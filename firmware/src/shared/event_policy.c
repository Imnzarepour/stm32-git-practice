#include "shared/event_policy.h"
#include "shared/queues.h"
#include "shared/log_record.h"
#include "managements/sms_management.h" /* SmsQueueItem - see its comment: the SMS Request Queue needs a tag to tell an EventAlertRequest apart from a direct SmsRequest */
#include "managements/call_management.h" /* CallQueueItem - same reasoning as SmsQueueItem, for the Call Request Queue */
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

/* Event is well under LOG_RECORD_MAX_PAYLOAD_BYTES - a compile error here
 * is the intended signal if Event ever grows past what a Log Record can
 * carry, instead of a silent truncation in persist_to_log() below. */
_Static_assert(sizeof(Event) <= LOG_RECORD_MAX_PAYLOAD_BYTES, "Event no longer fits in a LogRecord payload");

typedef struct {
    EventType event_type;
    bool      to_sms;
    bool      to_call;
    bool      to_server;
    bool      persist;
} EventPolicyRule;

static const EventPolicyRule POLICY_TABLE[] = {
    /* event_type                        sms    call   server persist */
    { EVENT_ACC_ON,                      false, false, true,  false },
    { EVENT_ACC_OFF,                     false, false, true,  false },
    { EVENT_DOOR_OPENED,                 false, false, true,  false },
    { EVENT_DOOR_CLOSED,                 false, false, false, false },
    { EVENT_SHOCK_DETECTED,              true,  true,  true,  true  },
    { EVENT_LOW_BATTERY_DETECTED,        true,  false, true,  true  },
    /* No documented severity/routing decision exists for this Event yet
     * (OPEN-ITEMS.md #15) - defaults to the exact same routing as its
     * closest analogous Event (EVENT_LOW_BATTERY_DETECTED, same battery
     * subsystem) rather than guessing a more severe one (e.g. matching
     * EVENT_SHOCK_DETECTED's call=true, on the theory a full disconnect
     * could be tamper-related) - revisit explicitly if that theory is
     * ever confirmed. */
    { EVENT_BATTERY_DISCONNECTED,        true,  false, true,  true  },
    { EVENT_GPS_FIX_ACQUIRED,            false, false, false, false },
    { EVENT_GPS_FIX_LOST,                false, false, true,  false },
    { EVENT_SMS_RECEIVED,                false, false, false, false },
    { EVENT_INCOMING_CALL,               false, false, false, false },
    { EVENT_FIRMWARE_UPDATE_COMPLETED,              false, false, true,  false },
    { EVENT_ACTUATOR_COMMAND_ACCEPTED,   false, false, true,  false },
    { EVENT_ACTUATOR_COMMAND_REJECTED,   false, false, true,  true  },
    { EVENT_ACTUATOR_FAULT,              false, false, true,  true  },
    { EVENT_SMS_SENT,                    false, false, true,  false },
    /* A Diagnostic Operation failure (CAN Bus Error/Timeout) is a technical/
     * system condition, not a safety alert someone needs an SMS/Call about -
     * same routing judgment already made for EVENT_ACTUATOR_FAULT above
     * (server + persist, no sms/call). No rate limiting decided yet: a
     * flaky ECU could in principle push one of these every periodic Read
     * Parameter cycle (~30s) - left as-is, same as every other Fault/Event
     * source in this codebase today (e.g. actuator_management.c's
     * report_output_failure() has the same characteristic, only a Reset
     * escalation threshold, no Event throttling). */
    { EVENT_DIAGNOSTIC_FAILURE,          false, false, true,  true  },
};
#define POLICY_TABLE_SIZE (sizeof(POLICY_TABLE) / sizeof(POLICY_TABLE[0]))

static const EventPolicyRule* find_rule(EventType type) {
    for (uint32_t i = 0; i < POLICY_TABLE_SIZE; i++) {
        if (POLICY_TABLE[i].event_type == type) {
            return &POLICY_TABLE[i];
        }
    }
    return NULL;
}

static void report_undefined_policy(const Event* event) {
    EventPolicyFaultRecord fault;
    fault.fault_code = EVENT_POLICY_FAULT_CODE_UNDEFINED;
    fault.related_event_type = (uint32_t)event->event_type;
    fault.timestamp = event->timestamp;
    (void)Queue_Push(Queues_GetFaultQueue(), &fault, sizeof(fault));
}

/* log-infrastructure.md section 0's own correction: persisting an Event
 * used to be a direct Storage_Write(STORAGE_EVENT, 0, ...) call here,
 * which is wrong on two counts - it is not the Log Record -> Log Queue
 * path the document requires, and offset 0 would overwrite the previous
 * persisted Event instead of appending. Event Policy now only builds the
 * Record and hands it to the Log Queue; Log Infrastructure
 * (infrastructure/log.c) owns everything about actually storing it. */
static void persist_to_log(const Event* event) {
    LogRecord record;
    record.source         = LOG_SOURCE_EVENT_POLICY;
    record.type            = (uint32_t)event->event_type;
    record.timestamp       = event->timestamp;
    record.payload_length = (uint32_t)sizeof(*event);
    memcpy(record.payload, event, sizeof(*event));

    (void)Queue_Push(Queues_GetLogQueue(), &record, sizeof(record));
}

static void dispatch(const Event* event, const EventPolicyRule* rule) {
    if (rule->to_sms) {
        SmsQueueItem item;
        item.kind = SMS_QUEUE_ITEM_ALERT;
        item.payload.alert.event_type = event->event_type;
        item.payload.alert.timestamp  = event->timestamp;
        (void)Queue_Push(Queues_GetSmsRequestQueue(), &item, sizeof(item));
    }
    if (rule->to_call) {
        CallQueueItem item;
        item.kind = CALL_QUEUE_ITEM_ALERT;
        item.payload.alert.event_type = event->event_type;
        item.payload.alert.timestamp  = event->timestamp;
        (void)Queue_Push(Queues_GetCallRequestQueue(), &item, sizeof(item));
    }
    if (rule->to_server) {
        (void)Queue_Push(Queues_GetServerEventQueue(), event, sizeof(*event));
    }
    if (rule->persist) {
        persist_to_log(event);
    }
}

void EventPolicy_Init(void) {
    /* چیزی برای مقداردهی اولیه لازم نیست - جدول Const و Static است. */
}

void EventPolicy_Process(void) {
    Event event;
    uint32_t length;

    if (!Queue_Pop(Queues_GetEventQueue(), &event, sizeof(event), &length)) {
        return;
    }

    const EventPolicyRule* rule = find_rule(event.event_type);
    if (rule == NULL) {
        report_undefined_policy(&event);
        return;
    }

    dispatch(&event, rule);
}