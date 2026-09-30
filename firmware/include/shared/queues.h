#ifndef SHARED_QUEUES_H
#define SHARED_QUEUES_H
#include "infrastructure/queue.h"

/*
 * Contract source:
 *  - docs/03-infrastructure-contracts/queue-infrastructure.md   (مکانیزم عمومی Queue)
 *  - docs/02-management-contracts/system-management.md          (Fault Queue)
 *
 * این فایل اولین "نمونه واقعی" صف‌هاست - queue.h/queue.c فقط مکانیزم عمومی
 * (کلاس) بودن، اینجا هفت Instance واقعی با Owner مشخص تعریف می‌شن.
 *
 * قانون: هر صف دقیقاً یک Owner/Consumer دارد. Producer می‌تواند متعدد باشد.
 *
 * TODO(open-item): ظرفیت‌های زیر تخمین اولیه‌اند، نه عدد نهایی - باید بعداً
 * بر اساس حجم واقعی Payload بازبینی بشن (نگاه کن OPEN-ITEMS.md).
 */

#define EVENT_QUEUE_CAPACITY_BYTES             512  /* Consumer: Event Policy/Dispatcher */
#define SMS_REQUEST_QUEUE_CAPACITY_BYTES      1536  /* Consumer: SMS Management - sized for ~7 SmsQueueItem entries (192B each, see sms_management.h) since 512B only fit ~2. Kept below TELEMETRY_QUEUE_CAPACITY_BYTES on purpose - see test_telemetry_queue_is_largest. */
#define CALL_REQUEST_QUEUE_CAPACITY_BYTES      256  /* Consumer: Call Management */
#define SERVER_EVENT_QUEUE_CAPACITY_BYTES      512  /* Consumer: Server Management */
#define TELEMETRY_QUEUE_CAPACITY_BYTES        2048  /* Consumer: Server Management */
#define FAULT_QUEUE_CAPACITY_BYTES             512  /* Consumer: System Management */
#define LOG_QUEUE_CAPACITY_BYTES                512  /* Consumer: Log Infrastructure - see log-infrastructure.md section 0/4. sizeof(LogRecord) is fixed (~48B), so this fits a handful of pending records; Queue's own Drop-Oldest policy applies same as every other queue if producers outrun Log_Process(). */
/* ActuatorRequest کوچک است (~12 بایت payload)، اما این صف عمداً کوچک نگه
 * داشته نشده: فرمان‌های حساس (ENGINE_CUT_OFF, DOOR_LOCK/UNLOCK, ALARM) از
 * این صف عبور می‌کنند و Drop-Oldest سراسری Queue Infrastructure نباید عملاً
 * فعال شود. ظرفیت هم‌ردیف SMS/Server Event Queue انتخاب شده تا حداقل چند
 * ده Request همزمان بدون Drop جا بگیرد (نگاه کن OPEN-ITEMS.md بند
 * Command Safety Policy - این فقط اندازه صف را حل می‌کند، نه Policy را). */
#define ACTUATOR_REQUEST_QUEUE_CAPACITY_BYTES   512  /* Consumer: Actuator Management */

void Queues_Init(void);

Queue* Queues_GetEventQueue(void);
Queue* Queues_GetSmsRequestQueue(void);
Queue* Queues_GetCallRequestQueue(void);
Queue* Queues_GetServerEventQueue(void);
Queue* Queues_GetTelemetryQueue(void);
Queue* Queues_GetFaultQueue(void);
Queue* Queues_GetActuatorRequestQueue(void);
Queue* Queues_GetLogQueue(void);

#endif