#ifndef SHARED_LOG_RECORD_H
#define SHARED_LOG_RECORD_H
#include <stdint.h>

/* Contract source: docs/03-infrastructure-contracts/log-infrastructure.md section 6.
 *
 * "Log Record: Source, Type, Timestamp, Payload" - the document says the
 * producing Management owns the meaning of Type/Payload, Log Infrastructure
 * never interprets them (section 2/6). This struct is the fixed-size shape
 * every producer builds and pushes onto the Log Queue (shared/queues.h);
 * Log Infrastructure (infrastructure/log.h) only ever moves it as opaque
 * bytes.
 *
 * TODO(open-item): LogSource below is a local/provisional enum, same
 * discipline as ResetSource had before its own gaps were logged
 * (OPEN-ITEMS.md 🟡 #2) - the document gives worked examples (Diagnostic,
 * Actuator, System) but never an exhaustive, official list. Kept here
 * deliberately separate from ResetSource (shared/reset_request.h): that
 * enum has no SYSTEM entry (System Management is Reset's executor, never
 * its requester), but System Management IS a Log producer (section 3's
 * own example: "System Management -> Boot Error -> Log Record"). */
typedef enum {
    LOG_SOURCE_SYSTEM,
    LOG_SOURCE_POWER,
    LOG_SOURCE_HARDWARE_INPUT,
    LOG_SOURCE_DIAGNOSTIC,
    LOG_SOURCE_GPS,
    LOG_SOURCE_SERVER,
    LOG_SOURCE_SMS,
    LOG_SOURCE_CALL,
    LOG_SOURCE_FIRMWARE_UPDATE,
    LOG_SOURCE_ACTUATOR,
    LOG_SOURCE_EVENT_POLICY /* not one of the 10 Managements, but a real producer - see event_policy.c */
} LogSource;

/* Implementation limit of this Contract's fixed-size Payload, not an
 * architecture decision - large enough for every producer this project
 * has today (Event is 12 bytes, BootErrorRecord is 8 bytes). A producer
 * with a bigger payload must trim it before pushing; Log Infrastructure
 * never resizes or rejects on its behalf (it does not know what Type
 * means, so it cannot judge whether trimming lost anything important). */
#define LOG_RECORD_MAX_PAYLOAD_BYTES 32u

typedef struct {
    LogSource source;
    uint32_t  type;           /* meaning is entirely up to `source` - Log Infrastructure never interprets it */
    uint32_t  timestamp;
    uint32_t  payload_length; /* bytes actually used in payload[] - may be 0 */
    uint8_t   payload[LOG_RECORD_MAX_PAYLOAD_BYTES];
} LogRecord;

#endif /* SHARED_LOG_RECORD_H */
