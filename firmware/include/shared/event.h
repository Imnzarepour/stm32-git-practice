#ifndef SHARED_EVENT_H
#define SHARED_EVENT_H
#include <stdint.h>

/*
 * Contract source: docs/01-architecture-notes/06-event-queue-architecture.md
 *                  docs/02-management-contracts/hardware-input-io-contract-DRAFT-A.md (§11)
 *
 * TODO(open-item): این لیست Event Catalog کامل نیست. هر Management که Event
 * جدید تعریف می‌کند باید مقدار آن را اینجا اضافه کند، نه در فایل خودش.
 */
typedef enum {
    EVENT_ACC_ON,
    EVENT_ACC_OFF,
    EVENT_DOOR_OPENED,
    EVENT_DOOR_CLOSED,
    EVENT_SHOCK_DETECTED,
    EVENT_LOW_BATTERY_DETECTED,
    /* docs/02-management-contracts/binary-protocol-final.md section 4:
     * wire Event Code 0x03 ("قطع باتری") - explicit user decision to keep
     * this a separate Event from EVENT_LOW_BATTERY_DETECTED, not reuse it
     * (OPEN-ITEMS.md #15). TODO(open-item): only the wire-mapping NAME was
     * decided - no detection condition (voltage threshold/algorithm
     * distinguishing "disconnected" from merely "low") exists anywhere in
     * hardware_input_management.c yet; nothing produces this Event today. */
    EVENT_BATTERY_DISCONNECTED,
    EVENT_GPS_FIX_ACQUIRED,
    EVENT_GPS_FIX_LOST,
    EVENT_SMS_RECEIVED,
    EVENT_SMS_SENT, /* Operation SMS_SEND -> SUCCESS, per state-operation-contract.md 17 */
    EVENT_INCOMING_CALL,
    EVENT_FIRMWARE_UPDATE_COMPLETED,
    EVENT_ACTUATOR_COMMAND_ACCEPTED,
    EVENT_ACTUATOR_COMMAND_REJECTED,
    EVENT_ACTUATOR_FAULT,
    /* diagnostic-management.md section 25: "Diagnostic -> Event/Fault
     * (مثل DIAGNOSTIC_FAILURE, ECU_UNAVAILABLE) -> Event Policy". A single,
     * general value covering any Operation that ends FAILED (CAN Bus Error
     * at Send time, or Response Timeout - see managements/
     * diagnostic_management.c's fail_current()) - the document's own
     * "مثل" (examples) wording does not fix a closed list, and splitting
     * DIAGNOSTIC_FAILURE from a narrower ECU_UNAVAILABLE would require
     * Protocol-level knowledge (which failure means the ECU itself is
     * gone) that belongs to the separate Diagnostic team, not this
     * architectural Catalog (CLAUDE.md). */
    EVENT_DIAGNOSTIC_FAILURE
} EventType;

typedef struct {
    EventType event_type;
    uint32_t  event_id;   /* برای Idempotency سمت سرور - At-Least-Once delivery */
    uint32_t  timestamp;  /* از Time_GetTick() یا Time_GetSystemTime() */
} Event;

#endif /* SHARED_EVENT_H */
