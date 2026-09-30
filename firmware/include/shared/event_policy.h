#ifndef SHARED_EVENT_POLICY_H
#define SHARED_EVENT_POLICY_H
#include "shared/event.h"
#include <stdint.h>

/*
 * مسئولیت Event Policy فقط Routing است، نه تولید محتوا و نه Persistence واقعی:
 *  - تصمیم می‌گیرد یک Event به کدام صف(های) مقصد برود و آیا باید Persist شود.
 *  - متن پیامک/شماره تماس را نمی‌سازد - آن مسئولیت SMS/Call Management خودشان است.
 *  - مصرف‌کننده تنها Event Queue است (تک‌مصرف‌کننده، طبق تصمیم بسته‌شده).
 *  - این یک Management نیست؛ یک Component سبک و بدون FSM است.
 */

typedef struct {
    EventType event_type;
    uint32_t  timestamp;
} EventAlertRequest;

/* TODO(open-item): ساختار رسمی Fault هنوز در OPEN-ITEMS.md باز است.
 * این یک فرم حداقلی و محلی است، فقط برای گزارش "Event بدون Policy"
 * (خطای پیکربندی/توسعه) - جایگزین Fault Contract نهایی نیست. */
#define EVENT_POLICY_FAULT_CODE_UNDEFINED 1001u

typedef struct {
    uint32_t fault_code;
    uint32_t related_event_type;
    uint32_t timestamp;
} EventPolicyFaultRecord;

void EventPolicy_Init(void);
void EventPolicy_Process(void); /* Non-Blocking - فقط یک Event در هر فراخوانی */

#endif