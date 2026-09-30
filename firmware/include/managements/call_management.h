#ifndef MGMT_CALL_H
#define MGMT_CALL_H
#include <stdbool.h>
#include <stdint.h>
#include "shared/event_policy.h" /* EventAlertRequest - see CallQueueItem below */
#include "managements/call_management_config.h" /* CALL_MAX_DESTINATION_LENGTH */

/* Contract source: docs/02-management-contracts/call-management.md
 * State/Operation pattern: docs/02-management-contracts/state-operation-contract.md
 * Depends only on the EC200U Manager Capability Contract
 * (infrastructure/ec200u_manager.h), which is still a draft
 * (OPEN-ITEMS.md, red, #1) - never AT Command/UART/Modem protocol
 * detail is assumed here beyond what that Contract already exposes. */

/* Contract section 5: "CallRequest { Destination }". destination is a
 * caller-owned pointer only for the duration of the Call_SubmitRequest()
 * call itself - the implementation copies it into a Management-owned
 * buffer before it can be queued, so the caller's own buffer/pointer
 * does not need to remain valid afterwards (same reasoning SMS Management
 * documents for its own SmsRequest, sms_management.h). */
typedef struct {
    const char* destination;
} CallRequest;

typedef enum {
    CALL_IDLE,
    CALL_OUTGOING,
    CALL_RINGING,
    CALL_CONNECTED,
    CALL_ENDING
} CallState;

/* The Call Request Queue (Queues_GetCallRequestQueue(), shared/queues.h)
 * carries two different kinds of item from two different kinds of
 * producer, exactly like SMS Management's SmsQueueItem (sms_management.h):
 *  - Event Policy (shared/event_policy.c) pushes an EventAlertRequest
 *    ("policy says this Event deserves a call" - it does not know phone
 *    numbers, that is Call Management's own job per the Contract).
 *  - A direct Call_SubmitRequest() pushes an explicit destination to dial.
 * Both are wrapped in this tagged envelope so Call_Process() can always
 * tell which one it just popped, instead of guessing from the raw byte
 * length of the popped item. destination is a fixed-size buffer (not the
 * public CallRequest's raw pointer) for the same reason SmsRequest uses
 * fixed arrays: this item may sit queued for several Process() cycles
 * after the caller's own buffer/pointer is no longer guaranteed valid. */
typedef struct {
    char destination[CALL_MAX_DESTINATION_LENGTH];
} CallRequestQueued;

typedef enum {
    CALL_QUEUE_ITEM_ALERT,
    CALL_QUEUE_ITEM_REQUEST
} CallQueueItemKind;

typedef struct {
    CallQueueItemKind kind;
    union {
        EventAlertRequest alert;
        CallRequestQueued request;
    } payload;
} CallQueueItem;

void      Call_Init(void);
void      Call_Process(void);
bool      Call_SubmitRequest(const CallRequest* request);
CallState Call_GetState(void);

/* Contract section 9: "Terminate Request -> Call Management -> EC200U
 * Manager -> Terminate Call". This is deliberately a direct function
 * rather than another item pushed onto the Call Request Queue
 * (Queues_GetCallRequestQueue()): unlike a CallRequest (a new call to
 * start, which may need to wait its turn behind whatever is currently
 * active), a Terminate always applies to whichever call is active right
 * now - there is never more than one (state-operation-contract.md
 * section 15, "One active operation"), so there is no ambiguity to
 * resolve via a queue and no payload to carry. Returns false if there is
 * currently nothing to terminate (Call_GetState() == CALL_IDLE);
 * otherwise the request is accepted for execution on a later
 * Call_Process() step - acceptance is not completion (state-operation-
 * contract.md section 8), the call may still take one or more
 * Call_Process() cycles to actually end. */
bool Call_Terminate(void);

/* SECURITY-SENSITIVE - see the warning block at the top of
 * call_management.c. Checks a phone number against the authorized-caller
 * list (infrastructure/configuration.h's DeviceConfig.access.allowed_phone_numbers -
 * Call Management owns the meaning of this list, Configuration
 * Infrastructure only stores it). Called from Call_Process()'s
 * incoming-call handling (Fail-Closed: an
 * incoming call is only ever Answered/accepted if this returns true for
 * its Caller ID) - see poll_and_advance()'s CALL_IDLE case in
 * call_management.c. Exposed publicly here as well so it can also be
 * unit-tested directly. */
bool Call_IsNumberAllowed(const char* number);

#endif
