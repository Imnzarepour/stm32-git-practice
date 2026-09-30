#include "unity.h"
#include "managements/system_management.h"
#include "Mockhardware_proxy_watchdog.h"
#include "Mockhardware_proxy_led.h"
#include "Mockpower_management.h"
#include "Mockgps_management.h"
#include "Mockec200u_manager.h"
#include "Mockstorage.h"
#include "infrastructure/storage.h"
#include "infrastructure/time_base.h"
#include "shared/queues.h"
#include "shared/log_record.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")
TEST_SOURCE_FILE("infrastructure/time_base.c")

/*
 * Per docs/03-infrastructure-contracts/hardware-proxy.md: only the
 * Hardware Proxy domains this Management actually touches (Watchdog,
 * LED) are mocked - never infrastructure/drivers/{watchdog,led}.h
 * underneath them. Power_GetMode()/GPS_GetFixState()/
 * EC200U_GetModuleState()/EC200U_GetNetworkState() are mocked too (CMock)
 * for the same reason test_gps_management.c mocks them: System Management
 * only ever depends on their public Contracts, never a specific
 * implementation - production code reads the real ones, this test controls
 * them directly.
 *
 * Scope reminder (see system_management.h): Reset Request validation/
 * execution and Fault Queue draining/counting are unchanged; this file
 * additionally covers the Watchdog Refresh Policy and the POWER/GPS/GSM
 * LED mapping. SIM LED and Buzzer are deliberately out of scope (see the
 * comments in system_management.c) and not tested here.
 */

static int s_reset_mcu_calls;
static int s_watchdog_init_calls;
static int s_watchdog_refresh_calls;

static void stub_reset_mcu(int cmock_num_calls) {
    (void)cmock_num_calls;
    s_reset_mcu_calls++;
}

static void stub_watchdog_init(int cmock_num_calls) {
    (void)cmock_num_calls;
    s_watchdog_init_calls++;
}

static void stub_watchdog_refresh(int cmock_num_calls) {
    (void)cmock_num_calls;
    s_watchdog_refresh_calls++;
}

/* --- LED spy: records every HWProxy_SetLed() call so tests can both
 * assert the value written and count how many times a given LED was
 * actually re-written (change-detection behavior). Indexed by
 * HWProxy_LedId, which is small/dense enough for a flat array. */
#define LED_ID_COUNT 4
static HWProxy_LedState s_last_led_value[LED_ID_COUNT];
static int               s_led_write_count[LED_ID_COUNT];

static void stub_set_led(HWProxy_LedId id, HWProxy_LedState state, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_last_led_value[id] = state;
    s_led_write_count[id]++;
}

/* Defaults for the three Data-path sources System Management reads every
 * Process() call - overridden per test via CMock's *_ExpectAndReturn /
 * changing these stub variables before calling System_Process() again. */
static PowerMode           s_stub_power_mode;
static GpsFixState         s_stub_gps_fix_state;
static EC200U_ModuleState  s_stub_module_state;
static EC200U_NetworkState s_stub_network_state;
static EC200U_SimCardState s_stub_sim_card_state;

/* --- Boot Error Record stub (bootloader.md §13, STORAGE_RECOVERY) ---
 * System Management's private BootErrorRecord layout (error_type,
 * error_code as two uint32_t) is not exposed by system_management.h - it
 * is a local/provisional struct (see the TODO(open-item) comment above it
 * in system_management.c), so these tests drive Storage_Read/Storage_Write
 * through raw bytes with the same layout, the same way
 * test_faults_from_other_managements_are_also_drained already treats Fault
 * Queue contents as an opaque shape it does not own. */
static StorageResult s_stub_storage_read_result;
static uint32_t      s_stub_boot_error_type;
static uint32_t      s_stub_boot_error_code;
static StorageObject s_last_storage_read_object;
static uint32_t      s_last_storage_read_offset;
static int           s_storage_write_calls;
static StorageObject s_last_storage_write_object;
static uint32_t      s_last_storage_write_offset;
static uint32_t      s_last_storage_write_value[2];

static StorageResult stub_storage_read(StorageObject object, uint32_t offset, void* buffer, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_last_storage_read_object = object;
    s_last_storage_read_offset = offset;
    if (s_stub_storage_read_result == STORAGE_OK) {
        uint32_t values[2];
        values[0] = s_stub_boot_error_type;
        values[1] = s_stub_boot_error_code;
        TEST_ASSERT_TRUE(length >= sizeof(values));
        memcpy(buffer, values, sizeof(values));
    }
    return s_stub_storage_read_result;
}

static StorageResult stub_storage_write(StorageObject object, uint32_t offset, const void* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_storage_write_calls++;
    s_last_storage_write_object = object;
    s_last_storage_write_offset = offset;
    TEST_ASSERT_TRUE(length <= sizeof(s_last_storage_write_value));
    memcpy(s_last_storage_write_value, data, length);
    return STORAGE_OK;
}

static PowerMode stub_power_get_mode(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_power_mode;
}

static GpsFixState stub_gps_get_fix_state(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_gps_fix_state;
}

static EC200U_ModuleState stub_get_module_state(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_module_state;
}

static EC200U_NetworkState stub_get_network_state(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_network_state;
}

static EC200U_SimCardState stub_get_sim_card_state(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_sim_card_state;
}

void setUp(void) {
    int i;

    Time_Init();
    Queues_Init();
    s_reset_mcu_calls        = 0;
    s_watchdog_init_calls    = 0;
    s_watchdog_refresh_calls = 0;

    for (i = 0; i < LED_ID_COUNT; i++) {
        s_last_led_value[i]  = HWPROXY_LED_OFF;
        s_led_write_count[i] = 0;
    }

    /* Steady, unremarkable defaults (NORMAL/no fix/module off) - individual
     * tests override these before calling System_Process() again where the
     * LED mapping itself is what's under test. */
    s_stub_power_mode     = POWER_NORMAL;
    s_stub_gps_fix_state  = GPS_NO_FIX;
    s_stub_module_state   = EC200U_MODULE_OFF;
    s_stub_network_state  = EC200U_NETWORK_UNKNOWN;
    s_stub_sim_card_state = EC200U_SIMCARD_NOT_READY;

    /* Default matches today's real behavior: STORAGE_RECOVERY has no
     * physical backend yet, so Storage_Read always fails and
     * check_boot_error() is a safe no-op - see system_management.c. */
    s_stub_storage_read_result  = STORAGE_ERROR;
    s_stub_boot_error_type      = 0u;
    s_stub_boot_error_code      = 0u;
    s_last_storage_read_object  = (StorageObject)-1;
    s_last_storage_read_offset  = 0xFFFFFFFFu;
    s_storage_write_calls       = 0;
    s_last_storage_write_object = (StorageObject)-1;
    s_last_storage_write_offset = 0xFFFFFFFFu;

    HWProxy_ResetMcu_StubWithCallback(stub_reset_mcu);
    HWProxy_InitWatchdog_StubWithCallback(stub_watchdog_init);
    HWProxy_RefreshWatchdog_StubWithCallback(stub_watchdog_refresh);
    HWProxy_SetLed_StubWithCallback(stub_set_led);
    HWProxy_Led_Process_Ignore(); /* Hardware Proxy-internal Blink timing - only the real _stm32f4.c implementation does anything with it, nothing to assert here */
    Power_GetMode_StubWithCallback(stub_power_get_mode);
    GPS_GetFixState_StubWithCallback(stub_gps_get_fix_state);
    EC200U_GetModuleState_StubWithCallback(stub_get_module_state);
    EC200U_GetNetworkState_StubWithCallback(stub_get_network_state);
    EC200U_GetSimCardState_StubWithCallback(stub_get_sim_card_state);
    Storage_Read_StubWithCallback(stub_storage_read);
    Storage_Write_StubWithCallback(stub_storage_write);

    System_Init();
}

void tearDown(void) {}

/* --- Watchdog --- */

void test_init_configures_the_watchdog_exactly_once(void) {
    TEST_ASSERT_EQUAL_INT(1, s_watchdog_init_calls);
}

void test_process_refreshes_the_watchdog_exactly_once_per_call(void) {
    System_Process();
    TEST_ASSERT_EQUAL_INT(1, s_watchdog_refresh_calls);

    System_Process();
    TEST_ASSERT_EQUAL_INT(2, s_watchdog_refresh_calls);
}

void test_watchdog_refresh_still_happens_with_a_pending_fault(void) {
    /* The Watchdog Policy is per-lap reachability of System_Process(), not
     * a Fault-count gate (see the comment above HWProxy_RefreshWatchdog()
     * in system_management.c: a Fault-count-based withhold policy is
     * explicitly left open, not implemented) - a queued Fault must not by
     * itself suppress the Refresh. */
    System_SubmitResetRequest(NULL); /* pushes one Fault */

    System_Process();

    TEST_ASSERT_EQUAL_INT(1, s_watchdog_refresh_calls);
}

/* --- LED: initial state --- */

void test_first_process_call_writes_all_four_leds(void) {
    System_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_led_write_count[HWPROXY_LED_POWER], "must write once even though the value did not 'change' from any prior value");
    TEST_ASSERT_EQUAL_INT(1, s_led_write_count[HWPROXY_LED_GPS]);
    TEST_ASSERT_EQUAL_INT(1, s_led_write_count[HWPROXY_LED_GSM]);
    TEST_ASSERT_EQUAL_INT(1, s_led_write_count[HWPROXY_LED_SIM]);
}

/* --- LED: Power --- */

void test_power_normal_turns_power_led_on(void) {
    s_stub_power_mode = POWER_NORMAL;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_ON, s_last_led_value[HWPROXY_LED_POWER]);
}

void test_power_low_power_blinks_power_led_slowly(void) {
    s_stub_power_mode = POWER_LOW_POWER;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_BLINK_SLOW, s_last_led_value[HWPROXY_LED_POWER]);
}

/* --- LED: GPS --- */

void test_gps_no_fix_turns_gps_led_off(void) {
    s_stub_gps_fix_state = GPS_NO_FIX;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_OFF, s_last_led_value[HWPROXY_LED_GPS]);
}

void test_gps_acquiring_blinks_gps_led_fast(void) {
    s_stub_gps_fix_state = GPS_FIX_ACQUIRING;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_BLINK_FAST, s_last_led_value[HWPROXY_LED_GPS]);
}

void test_gps_fix_valid_turns_gps_led_on(void) {
    s_stub_gps_fix_state = GPS_FIX_VALID;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_ON, s_last_led_value[HWPROXY_LED_GPS]);
}

/* --- LED: GSM/Cellular --- */

void test_module_off_turns_gsm_led_off(void) {
    s_stub_module_state  = EC200U_MODULE_OFF;
    s_stub_network_state = EC200U_NETWORK_UNKNOWN;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_OFF, s_last_led_value[HWPROXY_LED_GSM]);
}

void test_module_initializing_blinks_gsm_led_fast(void) {
    s_stub_module_state  = EC200U_MODULE_INITIALIZING;
    s_stub_network_state = EC200U_NETWORK_SEARCHING;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_BLINK_FAST, s_last_led_value[HWPROXY_LED_GSM]);
}

void test_module_ready_and_registered_turns_gsm_led_on(void) {
    s_stub_module_state  = EC200U_MODULE_READY;
    s_stub_network_state = EC200U_NETWORK_REGISTERED;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_ON, s_last_led_value[HWPROXY_LED_GSM]);
}

void test_module_ready_but_searching_blinks_gsm_led_fast(void) {
    s_stub_module_state  = EC200U_MODULE_READY;
    s_stub_network_state = EC200U_NETWORK_SEARCHING;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_BLINK_FAST, s_last_led_value[HWPROXY_LED_GSM]);
}

void test_module_ready_but_network_lost_blinks_gsm_led_slowly(void) {
    s_stub_module_state  = EC200U_MODULE_READY;
    s_stub_network_state = EC200U_NETWORK_LOST;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_BLINK_SLOW, s_last_led_value[HWPROXY_LED_GSM]);
}

void test_module_degraded_blinks_gsm_led_slowly(void) {
    s_stub_module_state  = EC200U_MODULE_DEGRADED;
    s_stub_network_state = EC200U_NETWORK_REGISTERED;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_BLINK_SLOW, s_last_led_value[HWPROXY_LED_GSM]);
}

void test_module_error_blinks_gsm_led_slowly(void) {
    s_stub_module_state  = EC200U_MODULE_ERROR;
    s_stub_network_state = EC200U_NETWORK_LOST;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_BLINK_SLOW, s_last_led_value[HWPROXY_LED_GSM]);
}

/* --- LED: SIM Card --- */

void test_sim_not_ready_turns_sim_led_off(void) {
    s_stub_sim_card_state = EC200U_SIMCARD_NOT_READY;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_OFF, s_last_led_value[HWPROXY_LED_SIM]);
}

void test_sim_ready_turns_sim_led_on(void) {
    s_stub_sim_card_state = EC200U_SIMCARD_READY;
    System_Process();
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_ON, s_last_led_value[HWPROXY_LED_SIM]);
}

/* --- LED: change-detection discipline --- */

void test_led_is_not_rewritten_while_status_is_unchanged(void) {
    s_stub_power_mode = POWER_NORMAL;

    System_Process();
    TEST_ASSERT_EQUAL_INT(1, s_led_write_count[HWPROXY_LED_POWER]);

    System_Process();
    System_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_led_write_count[HWPROXY_LED_POWER], "unchanged status must not re-write the LED");
}

void test_led_is_rewritten_when_status_changes(void) {
    s_stub_power_mode = POWER_NORMAL;
    System_Process();
    TEST_ASSERT_EQUAL_INT(1, s_led_write_count[HWPROXY_LED_POWER]);

    s_stub_power_mode = POWER_LOW_POWER;
    System_Process();
    TEST_ASSERT_EQUAL_INT(2, s_led_write_count[HWPROXY_LED_POWER]);
    TEST_ASSERT_EQUAL_INT(HWPROXY_LED_BLINK_SLOW, s_last_led_value[HWPROXY_LED_POWER]);
}

void test_sim_led_is_not_rewritten_while_status_is_unchanged(void) {
    s_stub_sim_card_state = EC200U_SIMCARD_READY;

    System_Process();
    System_Process();

    TEST_ASSERT_EQUAL_INT(1, s_led_write_count[HWPROXY_LED_SIM]);
}

static ResetRequest make_valid_system_reset(void) {
    ResetRequest request;
    request.type      = RESET_TYPE_SYSTEM_RESET;
    request.target    = "MCU";
    request.source    = RESET_SOURCE_ACTUATOR;
    request.reason    = RESET_REASON_RECOVERY_FAILED;
    request.timestamp = 1000;
    return request;
}

/* --- Init --- */

void test_fault_count_starts_at_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, System_GetFaultCount());
}

/* --- Valid SYSTEM_RESET --- */

void test_valid_system_reset_triggers_watchdog_reset(void) {
    ResetRequest request = make_valid_system_reset();

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT(1, s_reset_mcu_calls);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, System_GetFaultCount(), "A valid request must not be treated as a Fault");
}

void test_valid_system_reset_accepts_every_known_source(void) {
    /* GPS .. ACTUATOR (8 sources) must all be treated as authorized -
     * system-management.md does not define a finer per-source policy. */
    ResetSource sources[] = {
        RESET_SOURCE_GPS, RESET_SOURCE_SERVER, RESET_SOURCE_SMS, RESET_SOURCE_CALL,
        RESET_SOURCE_FIRMWARE_UPDATE, RESET_SOURCE_DIAGNOSTIC, RESET_SOURCE_HARDWARE_INPUT, RESET_SOURCE_ACTUATOR
    };
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        ResetRequest request = make_valid_system_reset();
        request.source = sources[i];
        System_SubmitResetRequest(&request);
    }

    TEST_ASSERT_EQUAL_INT(8, s_reset_mcu_calls);
    TEST_ASSERT_EQUAL_UINT32(0, System_GetFaultCount());
}

void test_valid_system_reset_accepts_the_firmware_install_requested_reason(void) {
    /* RESET_REASON_FIRMWARE_INSTALL_REQUESTED (added alongside
     * managements/firmware_update_management.c's FirmwareUpdate_SubmitInstallRequest() -
     * "مثل درب باز کن" precedent) must validate exactly like every other
     * known reason - NUM_RESET_REASONS must stay in sync with
     * reset_request.h. */
    ResetRequest request = make_valid_system_reset();
    request.source = RESET_SOURCE_FIRMWARE_UPDATE;
    request.reason = RESET_REASON_FIRMWARE_INSTALL_REQUESTED;

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT(1, s_reset_mcu_calls);
    TEST_ASSERT_EQUAL_UINT32(0, System_GetFaultCount());
}

/* --- Invalid requests: rejected, not executed --- */

void test_null_request_is_rejected_not_crashed(void) {
    System_SubmitResetRequest(NULL);

    TEST_ASSERT_EQUAL_INT(0, s_reset_mcu_calls);
}

void test_invalid_reset_type_is_rejected(void) {
    ResetRequest request = make_valid_system_reset();
    request.type = (ResetType)99;

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT(0, s_reset_mcu_calls);
}

void test_invalid_reset_source_is_rejected(void) {
    ResetRequest request = make_valid_system_reset();
    request.source = (ResetSource)99;

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT(0, s_reset_mcu_calls);
}

void test_invalid_reset_reason_is_rejected(void) {
    ResetRequest request = make_valid_system_reset();
    request.reason = (ResetReason)99;

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT(0, s_reset_mcu_calls);
}

void test_null_target_is_rejected(void) {
    ResetRequest request = make_valid_system_reset();
    request.target = NULL;

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT(0, s_reset_mcu_calls);
}

void test_system_reset_with_module_target_is_rejected(void) {
    /* reset-request-contract.md §6-3 explicit example: SYSTEM_RESET + EC200U -> Invalid */
    ResetRequest request = make_valid_system_reset();
    request.target = "EC200U";

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT(0, s_reset_mcu_calls);
}

void test_module_reset_with_mcu_target_is_rejected(void) {
    ResetRequest request = make_valid_system_reset();
    request.type   = RESET_TYPE_MODULE_RESET;
    request.target = "MCU";

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT(0, s_reset_mcu_calls);
}

/* --- Valid MODULE_RESET: accepted but not yet executable --- */

void test_valid_module_reset_is_accepted_but_not_executed_yet(void) {
    /* EC200U has no real reset Driver/Pin defined anywhere yet - this must
     * not be treated as an error (the Request itself is well-formed), but
     * it also must not fabricate a GPIO call. See system_management.c. */
    ResetRequest request = make_valid_system_reset();
    request.type   = RESET_TYPE_MODULE_RESET;
    request.target = "EC200U";

    System_SubmitResetRequest(&request);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_mcu_calls, "Watchdog reset is SYSTEM_RESET-only");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, System_GetFaultCount(), "A structurally valid Request must not be counted as a Fault");
}

/* --- Fault Queue draining --- */

void test_invalid_request_is_counted_as_fault(void) {
    System_SubmitResetRequest(NULL);

    System_Process();

    TEST_ASSERT_EQUAL_UINT32(1, System_GetFaultCount());
}

void test_process_drains_exactly_one_fault_per_call(void) {
    System_SubmitResetRequest(NULL);
    System_SubmitResetRequest(NULL);

    System_Process();
    TEST_ASSERT_EQUAL_UINT32(1, System_GetFaultCount());

    System_Process();
    TEST_ASSERT_EQUAL_UINT32(2, System_GetFaultCount());
}

void test_process_with_empty_fault_queue_does_nothing(void) {
    System_Process();

    TEST_ASSERT_EQUAL_UINT32(0, System_GetFaultCount());
}

void test_faults_from_other_managements_are_also_drained(void) {
    /* System Management does not care who produced the Fault or its exact
     * shape (that contract is still open) - it only drains/counts. */
    uint32_t some_other_fault_shape[3] = { 1, 2, 3 };
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetFaultQueue(), some_other_fault_shape, sizeof(some_other_fault_shape)));

    System_Process();

    TEST_ASSERT_EQUAL_UINT32(1, System_GetFaultCount());
}

/* --- Boot Error Record (bootloader.md §13, checked once at Init) --- */

void test_init_reads_the_boot_error_record_from_storage_recovery(void) {
    /* setUp() already called System_Init() once; this just verifies it
     * asked the Storage Contract for the right object, at offset 8
     * (BOOT_ERROR_RECORD_OFFSET, right after BootControl - see
     * bootloader/include/boot_control.h) - never a hardcoded physical
     * address. */
    TEST_ASSERT_EQUAL_INT(STORAGE_RECOVERY, s_last_storage_read_object);
    TEST_ASSERT_EQUAL_UINT32(8, s_last_storage_read_offset);
}

void test_boot_error_check_is_a_safe_noop_when_storage_recovery_has_no_backend(void) {
    /* Today's real state: STORAGE_RECOVERY is still an unimplemented
     * placeholder (test_storage.c), so Storage_Read always returns
     * STORAGE_ERROR - this must not be mistaken for a Fault. */
    TEST_ASSERT_EQUAL_UINT32(0, System_GetFaultCount());
    TEST_ASSERT_EQUAL_INT(0, s_storage_write_calls);
}

void test_boot_error_type_none_does_not_raise_a_fault(void) {
    s_stub_storage_read_result = STORAGE_OK;
    s_stub_boot_error_type     = 0u; /* BOOT_ERROR_TYPE_NONE - record present but empty */

    System_Init(); /* re-run with the new stub values */

    TEST_ASSERT_EQUAL_UINT32(0, System_GetFaultCount());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_storage_write_calls, "an already-empty record must not be re-written");
}

void test_present_boot_error_is_pushed_as_a_fault_and_then_cleared(void) {
    s_stub_storage_read_result = STORAGE_OK;
    s_stub_boot_error_type     = 1u; /* any non-zero = a real Boot Error is present */
    s_stub_boot_error_code     = 77u;

    System_Init();
    System_Process(); /* drains exactly one Fault per call */

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, System_GetFaultCount(), "a present Boot Error must be reported through the standard Fault path");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_storage_write_calls, "the record must be cleared exactly once after being reported");
    TEST_ASSERT_EQUAL_INT(STORAGE_RECOVERY, s_last_storage_write_object);
    TEST_ASSERT_EQUAL_UINT32(8, s_last_storage_write_offset);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_last_storage_write_value[0], "clear must write error_type back to NONE");
    TEST_ASSERT_EQUAL_UINT32(0, s_last_storage_write_value[1]);
}

void test_present_boot_error_also_pushes_a_persistent_log_record(void) {
    /* log-infrastructure.md section 3's own worked example: "System
     * Management -> Boot Error -> Log Record -> Log Queue" - independent
     * of the Fault Queue path covered by the test above. */
    s_stub_storage_read_result = STORAGE_OK;
    s_stub_boot_error_type     = 1u;
    s_stub_boot_error_code     = 77u;

    System_Init();

    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetLogQueue()));

    LogRecord log_record;
    uint32_t  length;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetLogQueue(), &log_record, sizeof(log_record), &length));
    TEST_ASSERT_EQUAL_INT(LOG_SOURCE_SYSTEM, log_record.source);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4002u, log_record.type, "must match SYSTEM_FAULT_CODE_BOOT_PROGRAMMING_FAILURE (private to system_management.c, not exposed via the header - kept in sync manually)");
    TEST_ASSERT_EQUAL_UINT32(8, log_record.payload_length); /* sizeof(BootErrorRecord): error_type + error_code */

    uint32_t carried_error_type;
    uint32_t carried_error_code;
    memcpy(&carried_error_type, &log_record.payload[0], sizeof(carried_error_type));
    memcpy(&carried_error_code, &log_record.payload[4], sizeof(carried_error_code));
    TEST_ASSERT_EQUAL_UINT32(1u, carried_error_type);
    TEST_ASSERT_EQUAL_UINT32(77u, carried_error_code);
}

void test_no_boot_error_pushes_nothing_to_the_log_queue(void) {
    /* default setUp() state: STORAGE_RECOVERY read fails (no backend yet) - System_Init() already ran once in setUp() */
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetLogQueue()));
}
