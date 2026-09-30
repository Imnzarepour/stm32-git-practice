#ifndef MGMT_SYSTEM_H
#define MGMT_SYSTEM_H
#include <stdint.h>
#include "shared/reset_request.h"

/* Contract source: docs/02-management-contracts/system-management.md
 * مسئولیت: System/Module Reset (متمرکز)، Watchdog، LED/Buzzer، Fault Queue متمرکز.
 * محدودیت: مالک Recovery اختصاصی سایر Managementها نیست.
 *
 * دامنه پیاده‌سازی فعلی:
 *   - System_SubmitResetRequest(): اعتبارسنجی کامل طبق reset-request-contract.md
 *     §6 (ResetType, Target, Type+Target, Source) + اجرای واقعی SYSTEM_RESET
 *     از طریق Hardware Proxy (Watchdog_Reset). MODULE_RESET فقط Validate
 *     می‌شود؛ Execution آن TODO است چون هیچ Driver/Pin واقعی برای Reset یک
 *     Module (مثلاً EC200U) هنوز تعریف نشده - نباید حدس زده شود.
 *   - System_Init(): علاوه بر Init واقعی Watchdog/LED، یک‌بار Boot Error
 *     Record را از STORAGE_RECOVERY می‌خواند (docs/03-infrastructure-contracts/
 *     bootloader.md §13) و در صورت وجود، آن را به یک Fault تبدیل و در Fault
 *     Queue قرار می‌دهد، سپس پاک می‌کند. STORAGE_RECOVERY هنوز Backend
 *     فیزیکی ندارد، پس این مسیر امروز همیشه بی‌خطر هیچ کاری نمی‌کند - نگاه
 *     کن check_boot_error() در system_management.c.
 *   - System_Process(): Fault Queue را Drain/Count می‌کند (Byte خام، بدون
 *     تفسیر فیلد چون ساختار رسمی Fault هنوز باز است، OPEN-ITEMS 🔴 #5)،
 *     LEDهای وضعیت را به‌روز می‌کند، و در انتها Watchdog را طبق Policy
 *     (نه بی‌قید‌وشرط) تغذیه می‌کند - جزئیات هر سه در system_management.c.
 *   - Watchdog: Init/Refresh هر دو از طریق Hardware Proxy
 *     (hardware_proxy_watchdog.h) هستند، هرگز مستقیم از firmware_main.c یا
 *     infrastructure/drivers/watchdog.h - رجوع کن به هاردور پراکسی.
 *   - LED: POWER/GPS/GSM/SIM از مسیر Data موجود (Power_GetMode/GPS_GetFixState/
 *     EC200U_GetModuleState+GetNetworkState/EC200U_GetSimCardState) محاسبه و
 *     فقط روی تغییر مقدار نوشته می‌شوند. EC200U_GetSimCardState() یک
 *     Capability حداقلی است که مخصوص همین LED به Contract پیش‌نویس EC200U
 *     Manager اضافه شد (تصمیم صریح کاربر). Buzzer کاملاً دست‌نخورده مانده -
 *     هیچ شرط فعال‌سازی‌ای در هیچ سندی تعریف نشده. */

void System_Init(void);
void System_Process(void); /* Non-Blocking - از Super Loop فراخوانی می‌شود */

/* پیاده‌سازی واقعی System_SubmitResetRequest در shared/reset_request.h اعلام شده */

/* Non-Blocking, Read-Only. تعداد Faultهایی که تا الان از Fault Queue Drain
 * شده‌اند - چون ساختار رسمی Fault باز است، این فقط یک شمارنده است، نه
 * جزئیات هر Fault (نگاه کن به کامنت بالا). */
uint32_t System_GetFaultCount(void);

#endif
