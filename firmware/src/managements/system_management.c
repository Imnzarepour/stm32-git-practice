#include "managements/system_management.h"
#include "managements/power_management.h"
#include "managements/gps_management.h"
#include "infrastructure/ec200u_manager.h"
#include "infrastructure/storage.h"
#include "hardware_proxy/hardware_proxy_watchdog.h"
#include "hardware_proxy/hardware_proxy_led.h"
#include "shared/queues.h"
#include "shared/log_record.h"
#include "infrastructure/time_base.h"
#include <string.h>
#include <stddef.h>

/* Contract source: docs/02-management-contracts/system-management.md
 *                  docs/02-management-contracts/reset-request-contract.md
 *
 * Scope of this implementation:
 *   - Reset Request validation + real SYSTEM_RESET execution (unchanged).
 *   - Fault Queue draining/counting only, no field interpretation (unchanged).
 *   - Watchdog: Init/Refresh now live entirely here, through Hardware Proxy
 *     - see the Watchdog Policy block above System_Process() below.
 *   - LED: POWER/GPS/GSM/SIM computed from the Data path every
 *     System_Process() call, only written to Hardware Proxy on change -
 *     see update_leds(). SIM uses EC200U_GetSimCardState(), a minimal
 *     Capability added to the EC200U Manager Contract specifically for
 *     this (see infrastructure/ec200u_manager.h). HWProxy_Led_Process() is
 *     called right after update_leds() every lap too - it is what actually
 *     makes a BLINK_SLOW/FAST LED blink on real hardware (a raw GPIO write
 *     cannot do that by itself); see hardware_proxy_led.h's own comment and
 *     hardware_proxy_led_stm32f4.c for the real implementation.
 *
 * Buzzer is deliberately still NOT done: HWProxy_SetBuzzer() is never
 * called anywhere in this file - no OPEN-ITEM or Contract section defines
 * when it should sound, and inventing a trigger (e.g. "beep on Fault")
 * would be a product decision, not an architecture one already closed. */

#define NUM_RESET_TYPES   2u /* Must stay in sync with ResetType   in reset_request.h */
#define NUM_RESET_SOURCES 8u /* Must stay in sync with ResetSource in reset_request.h */
#define NUM_RESET_REASONS 6u /* Must stay in sync with ResetReason in reset_request.h */

/* TODO(open-item): ساختار رسمی Fault هنوز در OPEN-ITEMS.md باز است (بند 🔴 #5).
 * این هم‌سبک ActuatorFaultRecord/EventPolicyFaultRecord است - محلی و موقت. */
#define SYSTEM_FAULT_CODE_INVALID_RESET_REQUEST 4001u

/* TODO(open-item): docs/03-infrastructure-contracts/bootloader.md بخش ۱۳ -
 * مقصد Boot Error Record حالا به یک Fault ترجمه می‌شود (تصمیم پیشنهادی خود
 * سند)، اما Struct/Offset زیر هنوز رسمی نیست - همان محدودیت
 * SYSTEM_FAULT_CODE_INVALID_RESET_REQUEST بالا (OPEN-ITEMS 🔴 #5) اینجا هم
 * صادق است. */
#define SYSTEM_FAULT_CODE_BOOT_PROGRAMMING_FAILURE 4002u

typedef struct {
    uint32_t fault_code;
    uint32_t reset_source; /* 0xFFFFFFFF اگر Source خودش نامعتبر بود */
    uint32_t timestamp;
} SystemFaultRecord;

/* --- Boot Error Record (bootloader.md §3/§12) ---
 * STORAGE_RECOVERY یک StorageObject مشترک بین Bootloader (Owner واقعی -
 * Active Bank/Boot Action را می‌نویسد، نگاه کن bootloader/include/boot_control.h)
 * و Application (فقط مصرف‌کننده همین زیر-رکورد Boot Error) است. Layout زیر
 * (BootControl در Offset 0، BootErrorRecord بعد از آن) هنوز جایی رسمی نشده -
 * فقط چون این دو طرف (اینجا و bootloader/include/) در یک نشست با هم
 * هماهنگ شدند، عدد ثابت مانده؛ همان محدودیت Fault Recordهای بالا (OPEN-ITEMS
 * 🔴 #5) اینجا هم صادق است - اگر Bootloader واقعی این Layout را عوض کرد،
 * این Offset هم باید همراهش عوض شود.
 * System Management هرگز Active Bank/Boot Action را نمی‌خواند یا نمی‌نویسد -
 * فقط همین زیر-رکورد. تا وقتی STORAGE_RECOVERY واقعاً Backend نگرفته
 * (memory-storage-infrastructure.md/OPEN-ITEMS)، Storage_Read این Offset
 * همیشه STORAGE_ERROR برمی‌گرداند و check_boot_error() بی‌خطر هیچ کاری
 * نمی‌کند - دقیقاً مثل RESET_TYPE_MODULE_RESET که Request را قبول می‌کند اما
 * هنوز چیزی را واقعاً اجرا نمی‌کند (نگاه کن System_SubmitResetRequest). */
#define BOOT_ERROR_RECORD_OFFSET 8u /* بعد از BootControl (BankId+BootAction = 2x uint32_t) */
#define BOOT_ERROR_TYPE_NONE     0u

typedef struct {
    uint32_t error_type; /* BOOT_ERROR_TYPE_NONE یعنی رکورد خالی/بدون خطا */
    uint32_t error_code;
} BootErrorRecord;

static uint32_t s_fault_count;

/* --- LED indicators (system-management.md §3-4/§5-3) ---
 * "System Management مسئول نمایش وضعیت است، مالک منطق داخلی GPS/شبکه/SIM/
 * Power نیست" - so every state below is read through the same public
 * Getter each owning Management already exposes for this exact purpose
 * (the Data path: Provider -> Latest Value -> Consumer), never a new
 * cross-Management call. Power_GetMode()/GPS_GetFixState() are already
 * consumed this same way by other Managements; EC200U_GetModuleState()/
 * GetNetworkState() are the same two Getters Server Management's own
 * update_connection_state() already reads - nothing new is exposed by any
 * of those three Contracts for this feature. */
static bool             s_led_initialized;
static HWProxy_LedState s_led_power_last;
static HWProxy_LedState s_led_gps_last;
static HWProxy_LedState s_led_gsm_last;
static HWProxy_LedState s_led_sim_last;

static HWProxy_LedState power_led_state(void) {
    return (Power_GetMode() == POWER_NORMAL) ? HWPROXY_LED_ON : HWPROXY_LED_BLINK_SLOW;
}

/* SIM LED - EC200U_GetSimCardState() is a minimal, just-added Capability
 * (infrastructure/ec200u_manager.h, still a draft extension per
 * OPEN-ITEMS #1) with only two states, so the mapping is direct: no
 * blink pattern, just present/usable or not. */
static HWProxy_LedState sim_led_state(void) {
    return (EC200U_GetSimCardState() == EC200U_SIMCARD_READY) ? HWPROXY_LED_ON : HWPROXY_LED_OFF;
}

static HWProxy_LedState gps_led_state(void) {
    switch (GPS_GetFixState()) {
        case GPS_NO_FIX:        return HWPROXY_LED_OFF;
        case GPS_FIX_ACQUIRING: return HWPROXY_LED_BLINK_FAST;
        case GPS_FIX_VALID:     return HWPROXY_LED_ON;
        default:                 return HWPROXY_LED_OFF;
    }
}

/* GSM/Cellular LED - system-management.md only names "وضعیت Cellular /
 * Network" as an example, with no concrete state->LED table, so this
 * mapping is this implementation's own judgment call (documented here,
 * not a re-opened architecture decision):
 *   MODULE_OFF                        -> OFF          (nothing to show)
 *   MODULE_INITIALIZING               -> BLINK_FAST   (booting)
 *   MODULE_DEGRADED / MODULE_ERROR    -> BLINK_SLOW   (module itself unwell)
 *   MODULE_READY + NETWORK_REGISTERED -> ON           (usable)
 *   MODULE_READY + NETWORK_LOST       -> BLINK_SLOW   (was usable, now degraded)
 *   MODULE_READY + SEARCHING/UNKNOWN  -> BLINK_FAST   (still trying) */
static HWProxy_LedState gsm_led_state(void) {
    EC200U_ModuleState  module  = EC200U_GetModuleState();
    EC200U_NetworkState network = EC200U_GetNetworkState();

    switch (module) {
        case EC200U_MODULE_OFF:          return HWPROXY_LED_OFF;
        case EC200U_MODULE_INITIALIZING: return HWPROXY_LED_BLINK_FAST;
        case EC200U_MODULE_DEGRADED:     return HWPROXY_LED_BLINK_SLOW;
        case EC200U_MODULE_ERROR:        return HWPROXY_LED_BLINK_SLOW;
        case EC200U_MODULE_READY:
            if (network == EC200U_NETWORK_REGISTERED) return HWPROXY_LED_ON;
            if (network == EC200U_NETWORK_LOST)        return HWPROXY_LED_BLINK_SLOW;
            return HWPROXY_LED_BLINK_FAST; /* SEARCHING or UNKNOWN */
        default:
            return HWPROXY_LED_OFF;
    }
}

/* Only calls HWProxy_SetLed() when the value actually changes (or on the
 * very first call after Init) - otherwise a real LED would be re-written
 * up to once per Super Loop iteration for no reason, the same
 * don't-touch-hardware-without-a-reason discipline the rest of this
 * firmware already applies (e.g. Actuator Management not re-driving an
 * output already at the requested level). */
static void update_led(HWProxy_LedId id, HWProxy_LedState desired, HWProxy_LedState* last) {
    if (!s_led_initialized || desired != *last) {
        HWProxy_SetLed(id, desired);
        *last = desired;
    }
}

static void update_leds(void) {
    update_led(HWPROXY_LED_POWER, power_led_state(), &s_led_power_last);
    update_led(HWPROXY_LED_GPS,   gps_led_state(),   &s_led_gps_last);
    update_led(HWPROXY_LED_GSM,   gsm_led_state(),   &s_led_gsm_last);
    update_led(HWPROXY_LED_SIM,   sim_led_state(),   &s_led_sim_last);
    s_led_initialized = true;
}

static bool is_valid_reset_type(ResetType type) {
    return (uint32_t)type < NUM_RESET_TYPES;
}

static bool is_valid_reset_source(ResetSource source) {
    return (uint32_t)source < NUM_RESET_SOURCES;
}

static bool is_valid_reset_reason(ResetReason reason) {
    return (uint32_t)reason < NUM_RESET_REASONS;
}

/* reset-request-contract.md §6-2/§6-3: Target باید با ResetType سازگار باشد.
 * تنها Targetهای شناخته‌شده طبق سند: SYSTEM_RESET->MCU, MODULE_RESET->EC200U. */
static bool is_valid_type_target_combo(ResetType type, const char* target) {
    if (target == NULL) {
        return false;
    }
    if (type == RESET_TYPE_SYSTEM_RESET) {
        return strcmp(target, "MCU") == 0;
    }
    if (type == RESET_TYPE_MODULE_RESET) {
        return strcmp(target, "EC200U") == 0;
    }
    return false;
}

static void report_invalid_request(const ResetRequest* request, uint32_t timestamp) {
    SystemFaultRecord fault;
    fault.fault_code   = SYSTEM_FAULT_CODE_INVALID_RESET_REQUEST;
    fault.reset_source = (request != NULL && is_valid_reset_source(request->source))
                              ? (uint32_t)request->source
                              : 0xFFFFFFFFu;
    fault.timestamp    = timestamp;
    (void)Queue_Push(Queues_GetFaultQueue(), &fault, sizeof(fault));
}

/* bootloader.md §13: Bootloader -> EEPROM Error Record -> Application ->
 * Read Boot Error -> [مسیر گزارش] -> Clear Boot Error. مسیر گزارش همان
 * Fault Queue استاندارد سیستم است (برای واکنش فوری/شمارش) - و از وقتی Log
 * Infrastructure اضافه شد (docs/03-infrastructure-contracts/
 * log-infrastructure.md), یک LogRecord هم می‌سازد تا سابقه پایدار آن هم
 * بماند (دقیقاً همان مثال خودِ سند در بخش ۳: «System Management → Boot
 * Error → Log Record → Log Queue»). این دو مسیر کاملاً مستقل‌اند - یکی
 * گذرا و برای واکنش سریع (Fault)، دیگری برای تاریخچه (Log)؛ حذف/شکست
 * یکی روی دیگری اثر نمی‌گذارد. فراخوانی می‌شود از System_Init(), دقیقاً
 * همانجایی که خود سند bootloader.md پیشنهاد می‌دهد ("هنگام نوشتن کد
 * Startup/System_Init() صریح تصمیم‌گیری شود"). امروز STORAGE_RECOVERY
 * هیچ Backend فیزیکی ندارد، پس Storage_Read همیشه STORAGE_ERROR
 * برمی‌گرداند و این تابع بی‌خطر هیچ کاری نمی‌کند. */
static void check_boot_error(void) {
    BootErrorRecord record;

    if (Storage_Read(STORAGE_RECOVERY, BOOT_ERROR_RECORD_OFFSET, &record, sizeof(record)) != STORAGE_OK) {
        return;
    }
    if (record.error_type == BOOT_ERROR_TYPE_NONE) {
        return;
    }

    uint32_t now = Time_GetTick();

    SystemFaultRecord fault;
    fault.fault_code   = SYSTEM_FAULT_CODE_BOOT_PROGRAMMING_FAILURE;
    fault.reset_source = 0xFFFFFFFFu; /* منشا Bootloader است، خارج از دامنه ResetSource */
    fault.timestamp    = now;
    (void)Queue_Push(Queues_GetFaultQueue(), &fault, sizeof(fault));

    LogRecord log_record;
    log_record.source         = LOG_SOURCE_SYSTEM;
    log_record.type            = SYSTEM_FAULT_CODE_BOOT_PROGRAMMING_FAILURE; /* همان معنای عددی بالا - یک عدد برای هر دو مسیر */
    log_record.timestamp       = now;
    log_record.payload_length = (uint32_t)sizeof(record);
    memcpy(log_record.payload, &record, sizeof(record)); /* خودِ BootErrorRecord خام - Log Infrastructure معنایش را تفسیر نمی‌کند */
    (void)Queue_Push(Queues_GetLogQueue(), &log_record, sizeof(log_record));

    /* فقط همین زیر-رکورد پاک می‌شود - نه کل STORAGE_RECOVERY (Active
     * Bank/Boot Action مال Bootloader است، اینجا هرگز دست‌نمی‌خورد). */
    BootErrorRecord cleared;
    cleared.error_type = BOOT_ERROR_TYPE_NONE;
    cleared.error_code = 0u;
    (void)Storage_Write(STORAGE_RECOVERY, BOOT_ERROR_RECORD_OFFSET, &cleared, sizeof(cleared));
}

void System_Init(void) {
    s_fault_count = 0u;

    HWProxy_InitWatchdog();
    check_boot_error();

    s_led_initialized = false;
    s_led_power_last  = HWPROXY_LED_OFF;
    s_led_gps_last    = HWPROXY_LED_OFF;
    s_led_gsm_last    = HWPROXY_LED_OFF;
    s_led_sim_last    = HWPROXY_LED_OFF;
}

void System_Process(void) {
    /* هر فراخوانی فقط یک Fault را از صف تخلیه می‌کند - Super Loop نباید
     * یک‌جا کل صف را پردازش کند (queue-infrastructure.md §22). محتوا عمداً
     * تفسیر نمی‌شود؛ فقط شمارش می‌شود (نگاه کن به کامنت بالای فایل).
     *
     * به‌روزرسانی: Log Infrastructure حالا وجود دارد
     * (infrastructure/log.h/.c) و صف مستقل خودش (Log Queue) را دارد - اما
     * این حلقه هنوز عمداً به آن وصل نشده. دلیلش دقیقاً همان محدودیتی است
     * که خط بالا می‌گوید: محتوای این Fault Queue بی‌شکل است (ساختار رسمی
     * Fault هنوز در OPEN-ITEMS.md 🔴 #5 باز است)، پس نمی‌شود این `discard[]`
     * خام را با معنای مشخص (Source/Type طبق LogRecord) به یک LogRecord
     * تبدیل کرد بدون حدس‌زدن. جاهایی که این‌طور نیست (مثلاً Boot Error در
     * check_boot_error() پایین همین فایل) از قبل مستقیم LogRecord خودشان
     * را می‌سازند و به Log Queue می‌فرستند - آن الگو درست است، نه این
     * حلقه عمومی. */
    uint8_t  discard[64];
    uint32_t length;

    if (Queue_Pop(Queues_GetFaultQueue(), discard, sizeof(discard), &length)) {
        s_fault_count++;
    }

    update_leds();
    HWProxy_Led_Process(); /* Advances Blink timing for any LED currently BLINK_SLOW/FAST - see hardware_proxy_led.h's own comment; a no-op on the host stub */

    /* --- Watchdog Policy (system-management.md §3-3) ---
     * Refreshed exactly once per Super Loop lap, from here - not
     * unconditionally from firmware_main.c before any Management has run
     * (the previous TODO this resolves). firmware_main.c calls
     * System_Process() second-to-last, right before EventPolicy_Process()
     * - so reaching this line is itself the evidence "every Management's
     * Process() call this lap already returned", which is exactly the
     * "system in a state of normal execution" condition the Contract asks
     * for, without System Management having to invent a cross-Management
     * health-check API it has no authority to define. If any earlier
     * Process() call hangs (violating the Non-Blocking rule everywhere
     * else in this firmware), this line - and therefore the Refresh - is
     * never reached again, and the hardware Watchdog's own timeout becomes
     * the "final unrecoverable-stop recovery mechanism" section 3-3
     * describes. A stronger policy (e.g. deliberately withholding Refresh
     * under a sustained Fault condition to force an early Reset) is a real
     * safety/product decision, not an architecture one already closed
     * anywhere - left open on purpose, not implemented here. */
    HWProxy_RefreshWatchdog();
}

void System_SubmitResetRequest(const ResetRequest* request) {
    uint32_t now = Time_GetTick();

    if (request == NULL
        || !is_valid_reset_type(request->type)
        || !is_valid_reset_source(request->source)
        || !is_valid_reset_reason(request->reason)
        || !is_valid_type_target_combo(request->type, request->target)) {
        report_invalid_request(request, now);
        return; /* اجرا نمی‌شود - طبق reset-request-contract.md §6 */
    }

    if (request->type == RESET_TYPE_SYSTEM_RESET) {
        HWProxy_ResetMcu(); /* روی سخت‌افزار واقعی برنمی‌گردد */
        return;
    }

    /* RESET_TYPE_MODULE_RESET: Request کاملاً معتبر است (Target=EC200U)،
     * اما هیچ Driver/Pin واقعی برای Reset یک Module هنوز تعریف نشده -
     * حدس زدنش (مثلاً یک GPIO دلخواه) دقیقاً همون اشتباهی است که این
     * پروژه صریحاً منع کرده. اجرای واقعی این بخش یک TODO باز می‌ماند. */
}

uint32_t System_GetFaultCount(void) {
    return s_fault_count;
}
