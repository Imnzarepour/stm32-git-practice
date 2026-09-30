#ifndef MGMT_ACTUATOR_H
#define MGMT_ACTUATOR_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/02-management-contracts/actuator-management.md
 *
 * !! هشدار ایمنی - TODO(open-item بحرانی، برای ENGINE_CUT_OFF همچنان باز) !!
 * هیچ Command Safety Policy واقعی برای فرمان‌های حساس هنوز تعریف نشده.
 * Actuator Management طبق Contract مسئول این تصمیم نیست (فقط اجرا می‌کند).
 * برای DOOR_LOCK/DOOR_UNLOCK، طبق تصمیم صریح کاربر (OPEN-ITEMS.md)، مسیر
 * ورودی (`shared/command_router.c` → `Actuator_SubmitRequest()`) از
 * `shared/command_safety_policy.h`'s `COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO`
 * عبور می‌کند - اما آن Policy هم فعلاً فقط Pass-Through بدون قید است، نه
 * یک بررسی واقعی (مثل سرعت/ACC). برای ENGINE_CUT_OFF این Gap هنوز کاملاً
 * باز و بسته (Fail-Closed) مانده - هیچ Domain/مسیر Server یا SMS به آن
 * وصل نشده. نگاه کن به OPEN-ITEMS.md.
 *
 * TODO(open-item 🟡#3، دوباره باز/بسته شد): تعداد خروجی فیزیکی بین ۳ و ۴
 * دو بار عوض شده - نسخه فعلی (۲۰۲۶-۰۹-۱۹): ۴ خروجی مستقل، طبق تصمیم صریح
 * کاربر بعد از تناقض با `docs/02-management-contracts/binary-protocol-final.md`
 * (Relay Mask ۴-بیتی هکار). ACTUATOR_DOOR_LOCK و ACTUATOR_DOOR_UNLOCK هرکدام
 * پایه فیزیکی مستقل خودشان را دارند (`actuator_management.c`'s
 * LINE_DOOR_LOCK/LINE_DOOR_UNLOCK)، دقیقاً هم‌الگوی Engine Cut-off/Alarm -
 * ActuatorCommand ورودی مقدار پایه را تعیین می‌کند. تاریخچه کامل و توجیه هر
 * دو تغییر در `docs/04-open-items/OPEN-ITEMS.md` بند 🟡#۳ ثبت شده - قبل از
 * تغییر دوباره این مورد، حتماً آنجا را بخوان. هنوز یک Placeholder نرم‌افزاری
 * است (بدون Pin واقعی CubeMX). */

typedef enum {
    ACTUATOR_ENGINE_CUT_OFF,
    ACTUATOR_ALARM,
    ACTUATOR_DOOR_LOCK,
    ACTUATOR_DOOR_UNLOCK
} ActuatorId;

typedef enum {
    ACTUATOR_CMD_ON,
    ACTUATOR_CMD_OFF,
    ACTUATOR_CMD_ACTIVATE,
    ACTUATOR_CMD_DEACTIVATE
} ActuatorCommand;

typedef struct {
    ActuatorId      actuator;
    ActuatorCommand command;
    uint32_t        timestamp;
} ActuatorRequest;

typedef struct {
    ActuatorCommand command;
    bool            active;
    uint32_t        timestamp;
} ActuatorState;

void          Actuator_Init(void);
void          Actuator_Process(void);
bool          Actuator_SubmitRequest(const ActuatorRequest* request); /* فقط اعتبارسنجی ساختاری، نه Safety Policy */
ActuatorState Actuator_GetState(ActuatorId id);

#endif
