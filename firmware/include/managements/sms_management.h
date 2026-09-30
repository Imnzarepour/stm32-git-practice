#ifndef MGMT_SMS_H
#define MGMT_SMS_H
#include <stdint.h>
#include <stdbool.h>
#include "shared/event_policy.h" /* EventAlertRequest - see SmsQueueItem below */
#include "managements/sms_management_config.h" /* SMS_MAX_DESTINATION_LENGTH/SMS_MAX_PAYLOAD_LENGTH - all SMS Management tunables live there */

/* Contract source: docs/02-management-contracts/sms-management.md
 * Pattern source:  docs/02-management-contracts/state-operation-contract.md
 *
 * OPEN QUESTION (asked separately, not guessed): the exact SMS Command
 * text format (keyword syntax) and sender authorization scheme are not
 * decided anywhere in docs/. Until answered, sms_try_parse_command() in
 * sms_management.c always reports "not a command", so every incoming SMS
 * safely falls back to an EVENT_SMS_RECEIVED Event - nothing is ever
 * forwarded to Actuator Management yet. This does not touch the separate,
 * still-open Command Safety Policy (OPEN-ITEMS.md red) - that gap is
 * about "is it safe to execute now", not about recognizing the command
 * text, and SMS Management still never evaluates it either way. */

/* Contract source/9: SmsRequest{Destination,Payload}. Fixed-size buffers
 * (not pointers) on purpose: once submitted this struct is copied byte-
 * for-byte into the SMS Request Queue (see shared/queues.h) and may sit
 * there for several Process() cycles - a caller's pointer/buffer is not
 * guaranteed to still be valid by the time it is actually sent. */
typedef struct {
    char     destination[SMS_MAX_DESTINATION_LENGTH];
    char     payload[SMS_MAX_PAYLOAD_LENGTH];
    uint32_t payload_length;
} SmsRequest;

/* The SMS Request Queue (Queues_GetSmsRequestQueue(), shared/queues.h)
 * carries two different kinds of item from two different kinds of
 * producer:
 *  - Event Policy pushes an EventAlertRequest ("policy says this Event
 *    deserves an SMS alert" - it does not know phone numbers or wording,
 *    that is SMS Management's own job per the Contract).
 *  - Any Management pushes a SmsRequest via SMS_SubmitRequest() (an
 *    explicit destination+payload to send as-is).
 * Both are wrapped in this tagged envelope so SMS_Process() can always
 * tell which one it just popped - a bare byte-size guess would be fragile
 * and this queue eventually feeds Actuator-relevant traffic. */
typedef enum {
    SMS_QUEUE_ITEM_ALERT,
    SMS_QUEUE_ITEM_REQUEST
} SmsQueueItemKind;

typedef struct {
    SmsQueueItemKind kind;
    union {
        EventAlertRequest alert;
        SmsRequest        request;
    } payload;
} SmsQueueItem;

void SMS_Init(void);
void SMS_Process(void);
bool SMS_SubmitRequest(const SmsRequest* request);

#endif
