#include "managements/sms_management.h"
#include "managements/sms_management_config.h" /* all tunable constants used below live in this one file */
#include "managements/actuator_management.h"
#include "managements/power_management.h"
#include "managements/gps_management.h"
#include "managements/call_management.h" /* Call_IsNumberAllowed() - reused directly, see sms_try_handle_position_request() */
#include "infrastructure/ec200u_manager.h"
#include "infrastructure/configuration.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"
#include "shared/command_safety_policy.h"
#include "shared/command_router.h"
#include <string.h>
#include <stdio.h>

/* Full contract: docs/02-management-contracts/sms-management.md
 * State/Operation pattern: docs/02-management-contracts/state-operation-contract.md
 *
 * One active outgoing send Operation at a time (contract 15 "One active
 * operation" model). While one is RUNNING, new items simply wait in the
 * SMS Request Queue - the queue itself is the "QUEUED" policy, no
 * separate rejection is needed. */

/* TODO(open-item): the system-wide Fault struct is still open
 * (OPEN-ITEMS.md red #5). This is a local, provisional shape for SMS
 * Send faults only, same pattern already used for EventPolicyFaultRecord
 * (shared/event_policy.h) and BatteryTelemetryRecord
 * (hardware_input_management.c) for their own still-open Contracts. */
typedef struct {
    uint32_t fault_code;
    uint32_t timestamp;
} SmsFaultRecord;

typedef enum {
    SMS_OP_IDLE,
    SMS_OP_SENDING,
    /* NEW (real EC200U hardware): a real AT+CMGS round trip (module prompt
     * + SMSC network delivery) cannot complete inside the single
     * EC200U_SMS_Send() call the old EC200U Simulator's instant fake
     * success allowed - EC200U_SMS_Send() now only means "accepted, an AT
     * sequence just started in the background" (EC200U_OK) or an
     * immediate local rejection (anything else). This state polls
     * EC200U_SMS_GetSendStatus() (infrastructure/ec200u_manager.h, added
     * for exactly this reason) until the real result is known, instead of
     * treating EC200U_SMS_Send()'s own return as final. */
    SMS_OP_WAITING_RESULT
} SmsOperationState;

static SmsOperationState s_op_state;
static EC200U_SmsData    s_current_send;                          /* destination/payload below alias the two buffers here */
static char              s_current_destination[SMS_MAX_DESTINATION_LENGTH];
static char              s_current_payload[SMS_MAX_PAYLOAD_LENGTH];
static uint32_t          s_retry_count;
static bool              s_retry_pending;                          /* true while waiting out SMS_SEND_RETRY_DELAY_MS */
static TimeTick          s_next_attempt_tick;
static uint32_t          s_next_event_id;

/* Contract 14 (SMS و Power Mode): outgoing send attempts are spaced out
 * further in LOW_POWER ("Reduced SMS Activity"); incoming reception is
 * never touched by Power Mode (contract 14, explicit). Same
 * throttle-since-last-attempt shape as server_management.c's
 * s_send_throttled/s_last_send_tick, but scoped to SMS's own outgoing
 * attempts only. s_send_throttled stays false until the very first
 * attempt so the first ever send is never delayed waiting for a
 * "last attempt" that never happened. */
static bool     s_send_throttled;
static TimeTick s_last_send_tick;

static void emit_event(EventType type) {
    Event e;
    e.event_type = type;
    e.event_id   = s_next_event_id++;
    e.timestamp  = Time_GetTick();
    (void)Queue_Push(Queues_GetEventQueue(), &e, sizeof(e));
}

static void push_send_fault(void) {
    SmsFaultRecord fault;
    fault.fault_code = SMS_FAULT_CODE_SEND_FAILED;
    fault.timestamp  = Time_GetTick();
    (void)Queue_Push(Queues_GetFaultQueue(), &fault, sizeof(fault));
}

static const char* alert_text_for_event(EventType type) {
    switch (type) {
        case EVENT_SHOCK_DETECTED:       return "ALERT: SHOCK_DETECTED";
        case EVENT_LOW_BATTERY_DETECTED: return "ALERT: LOW_BATTERY_DETECTED";
        default:                         return "ALERT: EVENT";
    }
}

/* TODO(open question, asked separately - not guessed): SMS command text
 * syntax and sender authorization are not defined anywhere in docs/.
 * Until answered, every incoming SMS is treated as a plain message
 * (EVENT_SMS_RECEIVED) and this always reports "not a command" - the
 * safe default, since nothing can reach Actuator_SubmitRequest() by
 * guessing. This is independent of the separate, still-open Command
 * Safety Policy (OPEN-ITEMS.md red): that gap is about "is it safe to
 * execute right now", which SMS Management never evaluates either way. */
static bool sms_try_parse_command(const EC200U_SmsData* sms, ActuatorRequest* out_request) {
    (void)sms;
    (void)out_request;
    return false;
}

/* strlen() would be unsafe here: destination may come straight from a
 * SmsRequest.destination field (see sms_management.h) that a careless
 * caller filled edge-to-edge without leaving room for a NUL - reading
 * past a non-NUL-terminated fixed array is undefined behavior. Capping
 * the scan at max makes the same guarantee strlen() gives when the
 * input IS properly terminated, and degrades safely (truncates) when it
 * is not, instead of over-reading. */
static uint32_t bounded_strlen(const char* s, uint32_t max) {
    uint32_t len = 0;
    while (len < max && s[len] != '\0') {
        len++;
    }
    return len;
}

static void start_send(const char* destination, const char* payload, uint32_t payload_length) {
    uint32_t dest_len = bounded_strlen(destination, SMS_MAX_DESTINATION_LENGTH - 1u);
    memcpy(s_current_destination, destination, dest_len);
    s_current_destination[dest_len] = '\0';

    if (payload_length >= SMS_MAX_PAYLOAD_LENGTH) {
        payload_length = SMS_MAX_PAYLOAD_LENGTH - 1u;
    }
    memcpy(s_current_payload, payload, payload_length);
    s_current_payload[payload_length] = '\0';

    s_current_send.destination    = s_current_destination;
    s_current_send.payload        = s_current_payload;
    s_current_send.payload_length = payload_length;

    s_op_state      = SMS_OP_SENDING;
    s_retry_count   = 0;
    s_retry_pending = false;
}

/* Formats GPS Management's current Position as "POS:<lat>.<frac>,<lon>.<frac>"
 * (decimal degrees, 6-decimal precision - matches the Microdegrees Fixed-
 * Point scale gps_management.c documents as its own placeholder scale), or
 * SMS_POSITION_REPLY_NO_FIX_TEXT if there has never been a valid fix yet.
 * TODO(ask user): this exact wording/precision is provisional, same status
 * as SMS_POSITION_REQUEST_KEYWORD (sms_management_config.h) - a real
 * product decision on format/units/localization has not been made.
 * Splitting degrees/fraction by hand (not a plain "%ld.%06ld" on the raw
 * microdegree value) is required for correctness on negative values
 * (south/west): C's / and % both truncate toward zero, so e.g.
 * -6300000 already yields deg=-6, frac=-300000 - the frac sign is
 * dropped explicitly below so it never prints as "-6.-300000". Returns
 * the number of characters written (not counting the NUL terminator),
 * always less than out_size. */
static uint32_t format_position_reply(const GpsPosition* position, char* out, uint32_t out_size) {
    int written;

    if (!position->valid) {
        written = snprintf(out, out_size, "%s", SMS_POSITION_REPLY_NO_FIX_TEXT);
    } else {
        long lat_deg  = position->latitude / 1000000;
        long lat_frac = position->latitude % 1000000;
        long lon_deg  = position->longitude / 1000000;
        long lon_frac = position->longitude % 1000000;
        if (lat_frac < 0) lat_frac = -lat_frac;
        if (lon_frac < 0) lon_frac = -lon_frac;

        written = snprintf(out, out_size, "POS:%ld.%06ld,%ld.%06ld", lat_deg, lat_frac, lon_deg, lon_frac);
    }

    if (written < 0) {
        return 0;
    }
    return ((uint32_t)written < out_size) ? (uint32_t)written : (out_size - 1u);
}

/* First (and so far only) concrete SMS command this Management recognizes -
 * gated behind an exact keyword match, NOT a general parser (see the
 * long-standing "OPEN QUESTION" comment on sms_try_parse_command() above -
 * that gap is still fully open for Actuator-bound commands; this is a
 * narrower, explicitly non-Actuating exception, same reasoning
 * COMMAND_DOMAIN_DIAGNOSTIC already uses via shared/command_safety_policy.h).
 * Returns true if `sms` WAS this command (handled one way or another -
 * caller must not also fall through to EVENT_SMS_RECEIVED), false if it
 * was not, so the caller can keep checking other possibilities.
 *
 * Two independent gates, both required, same "layer your safety checks"
 * shape Call Management already uses for incoming calls
 * (Call_IsNumberAllowed() in call_management.c):
 *   1. Call_IsNumberAllowed(sender) - the sender must already be on the
 *      shared Allowed Phone Numbers list (infrastructure/configuration.h's
 *      DeviceConfig.access.allowed_phone_numbers). Reused directly from
 *      Call Management rather than duplicating the same list/loop here -
 *      "who is allowed to interact with this device" is one list, not a
 *      separate one per capability. TODO(ask user): confirm this reuse is
 *      the right call, or whether SMS should eventually get its own list.
 *   2. CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_GPS_POSITION_READ) -
 *      the shared Command Safety Policy "door" (shared/command_safety_policy.h) -
 *      unconditionally true today (a location READ is not Actuation), but
 *      this is explicitly the seam OPEN-ITEMS.md tracks for a real policy
 *      later (e.g. rate limiting, time-of-day).
 * Either gate failing means: recognized as the command, but silently
 * dropped - Fail-Closed, no reply at all (never confirm/deny to an
 * unauthorized sender that this keyword is valid syntax).
 *
 * NOTE (needs future work, not assumed silently): EC200U_SmsData.destination
 * is read here as the SENDER's number for an INCOMING message - the same
 * struct/field is also used as the RECIPIENT for an OUTGOING
 * EC200U_SMS_Send() call (see start_send() above). Nothing in
 * ec200u-manager.md documents this direction-dependent meaning explicitly -
 * flagged here as a real gap in that Contract, not silently assumed. */
static bool sms_try_handle_position_request(const EC200U_SmsData* sms) {
    SmsRequest  reply;
    GpsPosition position;

    if (bounded_strlen(sms->payload, SMS_MAX_PAYLOAD_LENGTH) != (uint32_t)strlen(SMS_POSITION_REQUEST_KEYWORD)
        || strncmp(sms->payload, SMS_POSITION_REQUEST_KEYWORD, strlen(SMS_POSITION_REQUEST_KEYWORD)) != 0) {
        return false; /* not this command - caller falls through to plain-message handling */
    }

    if (sms->destination == NULL || !Call_IsNumberAllowed(sms->destination)) {
        return true; /* recognized, sender not authorized - see the gate comment above */
    }
    if (!CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_GPS_POSITION_READ)) {
        return true; /* Policy gate closed - see shared/command_safety_policy.c */
    }

    position = GPS_GetPosition();

    memset(&reply, 0, sizeof(reply));
    strncpy(reply.destination, sms->destination, SMS_MAX_DESTINATION_LENGTH - 1u); /* reply goes back to the SAME number that asked - never the fixed Event-Policy-alert destination (a different concept) */
    reply.payload_length = format_position_reply(&position, reply.payload, sizeof(reply.payload));

    /* Queued via the normal Request path (state-operation-contract.md
     * section 15's "One active Operation" model), never sent directly
     * from here - a send may already be RUNNING for something else, and
     * clobbering s_current_destination/s_current_payload mid-flight would
     * corrupt it. */
    (void)SMS_SubmitRequest(&reply);
    return true;
}

/* Second concrete SMS command - Door Lock/Unlock via Diagnostic's Actuator
 * Test, reached through shared/command_router.c's
 * COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST. UNLIKE sms_try_handle_position_request()
 * above, this domain physically actuates the vehicle - it is the exact
 * scenario OPEN-ITEMS.md's 🔴 critical Command Safety Policy item warns
 * about. It is open today only because of an explicit, deliberate user
 * decision (tracked in detail in OPEN-ITEMS.md) to open the data path
 * end-to-end before a real policy exists - shared/command_safety_policy.c
 * itself documents that this is Pass-Through by request, not by that
 * file's own judgment. Same two gates as the Position Request path
 * (Call_IsNumberAllowed() then CommandSafetyPolicy_IsAllowed()), same
 * Fail-Closed-silent behavior on either gate failing - see that
 * function's own comment for the full reasoning, not repeated here.
 *
 * No reply SMS is sent on acceptance (unlike the Position Request path,
 * which has data to send back) - this command's result is not reported
 * back to the sender at all in this pass. TODO(ask user, tracked in
 * OPEN-ITEMS.md): whether the user wants a confirmation SMS once
 * Diagnostic_GetResponse() actually completes is an open, unasked
 * question - polling that response is not wired anywhere from this file
 * today (only server_management.c's poll_diagnostic_response() does, for
 * Server-originated commands). */
static bool sms_try_handle_door_command(const EC200U_SmsData* sms) {
    uint32_t payload_len = bounded_strlen(sms->payload, SMS_MAX_PAYLOAD_LENGTH);
    bool     is_open;

    if (payload_len == (uint32_t)strlen(SMS_DOOR_OPEN_KEYWORD)
        && strncmp(sms->payload, SMS_DOOR_OPEN_KEYWORD, payload_len) == 0) {
        is_open = true;
    } else if (payload_len == (uint32_t)strlen(SMS_DOOR_CLOSE_KEYWORD)
               && strncmp(sms->payload, SMS_DOOR_CLOSE_KEYWORD, payload_len) == 0) {
        is_open = false;
    } else {
        return false; /* not this command - caller falls through to plain-message handling */
    }

    if (sms->destination == NULL || !Call_IsNumberAllowed(sms->destination)) {
        return true; /* recognized, sender not authorized - Fail-Closed, silently dropped */
    }
    if (!CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST)) {
        return true; /* Policy gate closed - see shared/command_safety_policy.c */
    }

    {
        Command command;
        command.domain    = COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST;
        command.operation = 0u; /* unused for this domain - see command_router.c's route_to_diagnostic_actuator_test() */
        command.target     = SMS_DOOR_ACTUATOR_TARGET_ECU_ID;
        command.parameter  = is_open ? SMS_DOOR_ACTUATOR_PARAMETER_OPEN : SMS_DOOR_ACTUATOR_PARAMETER_CLOSE;
        (void)CommandRouter_Submit(&command); /* BUSY (Diagnostic's two slots full) is silently dropped too, same minimal scope as every other failure path here */
    }
    return true;
}

/* Third concrete SMS command - the second, independent execution
 * mechanism for the exact same physical action as
 * sms_try_handle_door_command() above (Door Lock/Unlock), this time via
 * Actuator Management's own direct GPIO relay output instead of
 * Diagnostic/CAN - explicit user request for two separate commands
 * ("۲ کامند جدا"), see command_safety_policy.h's comment on
 * COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO. Same two gates, same Fail-Closed-
 * silent behavior, same no-reply-SMS choice as that function - not
 * repeated here, see its own comment for the full reasoning. */
static bool sms_try_handle_door_gpio_command(const EC200U_SmsData* sms) {
    uint32_t payload_len = bounded_strlen(sms->payload, SMS_MAX_PAYLOAD_LENGTH);
    bool     is_open;

    if (payload_len == (uint32_t)strlen(SMS_DOOR_OPEN_GPIO_KEYWORD)
        && strncmp(sms->payload, SMS_DOOR_OPEN_GPIO_KEYWORD, payload_len) == 0) {
        is_open = true;
    } else if (payload_len == (uint32_t)strlen(SMS_DOOR_CLOSE_GPIO_KEYWORD)
               && strncmp(sms->payload, SMS_DOOR_CLOSE_GPIO_KEYWORD, payload_len) == 0) {
        is_open = false;
    } else {
        return false; /* not this command - caller falls through to plain-message handling */
    }

    if (sms->destination == NULL || !Call_IsNumberAllowed(sms->destination)) {
        return true; /* recognized, sender not authorized - Fail-Closed, silently dropped */
    }
    if (!CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO)) {
        return true; /* Policy gate closed - see shared/command_safety_policy.c */
    }

    {
        Command command;
        command.domain    = COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO;
        command.operation = 0u; /* unused for this domain - see command_router.c's route_to_actuator_door_gpio() */
        command.target     = 0u; /* unused - Actuator Management addresses by ActuatorId, not an ECU id */
        command.parameter  = is_open ? 1u : 0u; /* command_router.c: 1=unlock/open, 0=lock/close */
        (void)CommandRouter_Submit(&command);
    }
    return true;
}

/* Pops at most one incoming SMS per Process() cycle from EC200U Manager
 * (a pull/latest-value API, not a Queue - see ec200u-manager.md 16). If
 * the module is holding more than one, the next Process() cycle drains
 * the next one, same as draining any other queue one item at a time. */
static void process_incoming(void) {
    EC200U_SmsData sms;
    if (EC200U_SMS_GetReceived(&sms) != EC200U_OK) {
        return;
    }
    if (sms.payload == NULL || sms.payload_length == 0u) {
        return; /* Structurally invalid - contract 6, Message Format */
    }

    if (sms_try_handle_position_request(&sms)) {
        return;
    }
    if (sms_try_handle_door_command(&sms)) {
        return;
    }
    if (sms_try_handle_door_gpio_command(&sms)) {
        return;
    }

    ActuatorRequest actuator_request;
    if (sms_try_parse_command(&sms, &actuator_request)) {
        /* SMS Management only forwards the Request; it never decides
         * whether executing it is safe right now or who is allowed to -
         * that belongs to Actuator Management / the still-open Command
         * Safety Policy (OPEN-ITEMS.md red), not here. */
        (void)Actuator_SubmitRequest(&actuator_request);
        return;
    }

    emit_event(EVENT_SMS_RECEIVED);
}

/* Only called while IDLE. Pops one item from the SMS Request Queue and
 * turns it into a queued outgoing send - see SmsQueueItem's comment in
 * sms_management.h for why the two producer kinds need a tag. */
static void try_start_next(void) {
    if (s_op_state != SMS_OP_IDLE) {
        return;
    }

    SmsQueueItem item;
    uint32_t     length;
    if (!Queue_Pop(Queues_GetSmsRequestQueue(), &item, sizeof(item), &length)) {
        return;
    }

    if (item.kind == SMS_QUEUE_ITEM_ALERT) {
        const char* text = alert_text_for_event(item.payload.alert.event_type);
        /* Destination now comes from Configuration Infrastructure (EEPROM
         * via STORAGE_CONFIG), replacing the old compile-time
         * SMS_ALERT_DESTINATION_PLACEHOLDER - see configuration.h's
         * ConfigAccess comment. */
        start_send(Configuration_Get()->access.sms_alert_destination, text, (uint32_t)strlen(text));
    } else {
        start_send(item.payload.request.destination, item.payload.request.payload, item.payload.request.payload_length);
    }
}

static void fail_current_attempt(void); /* defined below advance_waiting_result() - forward-declared since advance_send() also calls it */

/* Advances the current SENDING Operation by exactly one step per
 * Process() cycle - contract 8: "Request -> Start Send -> Save State ->
 * Return", never waiting inline for the result. */
static void advance_send(void) {
    if (s_op_state != SMS_OP_SENDING) {
        return;
    }

    if (s_retry_pending) {
        if (!Time_Elapsed(s_next_attempt_tick, SMS_SEND_RETRY_DELAY_MS)) {
            return; /* still in the retry backoff window */
        }
        s_retry_pending = false;
    }

    /* Contract 14: additional Power-Mode pacing, independent of and on
     * top of the retry backoff above. Applies to every attempt (first
     * try of a new item as well as a retry), not just failures, so a
     * fast chain of successful sends is also spaced out in LOW_POWER. */
    if (Power_GetMode() == POWER_LOW_POWER && s_send_throttled) {
        if (!Time_Elapsed(s_last_send_tick, SMS_LOW_POWER_MIN_SEND_INTERVAL_MS)) {
            return; /* still within the LOW_POWER minimum send interval */
        }
    }

    EC200U_Result result = EC200U_SMS_Send(&s_current_send);

    if (result == EC200U_BUSY) {
        return; /* EC200U's own SMS subsystem is mid-sequence for an unrelated reason - transient, retried next Process() call, not counted against SMS_SEND_MAX_RETRIES (same "BUSY is not a failure" convention FileTransfer/Diagnostic already use) */
    }

    s_last_send_tick = Time_GetTick();
    s_send_throttled = true;

    if (result == EC200U_OK) {
        s_op_state = SMS_OP_WAITING_RESULT; /* accepted - the real result arrives asynchronously, see advance_waiting_result() below */
        return;
    }

    fail_current_attempt();
}

/* Only called while WAITING_RESULT - polls the real outcome of the
 * AT+CMGS sequence EC200U_SMS_Send() started. Not throttled by
 * SMS_SEND_RETRY_DELAY_MS/LOW_POWER pacing (those only govern spacing
 * BETWEEN attempts, see advance_send()'s own checks) - polling an
 * already-in-flight send every Process() call costs nothing (a single
 * state read, no AT traffic) and must not add artificial delay on top of
 * the real SMSC round trip. */
static void advance_waiting_result(void) {
    EC200U_Result status;

    if (s_op_state != SMS_OP_WAITING_RESULT) {
        return; /* nothing in flight - must not call EC200U_SMS_GetSendStatus() every idle cycle */
    }

    status = EC200U_SMS_GetSendStatus();

    if (status == EC200U_BUSY) {
        return; /* still in progress */
    }
    if (status == EC200U_OK) {
        emit_event(EVENT_SMS_SENT);
        s_op_state = SMS_OP_IDLE;
        return;
    }

    fail_current_attempt();
}

/* Shared by both failure paths above (an immediate local rejection from
 * EC200U_SMS_Send(), or a real AT+CMGS failure reported later via
 * EC200U_SMS_GetSendStatus()) - same Retry/Fault bookkeeping either way. */
static void fail_current_attempt(void) {
    s_retry_count++;
    if (s_retry_count > SMS_SEND_MAX_RETRIES) {
        push_send_fault();
        s_op_state = SMS_OP_IDLE; /* local recovery: drop this item, move on - no Reset Request for an ordinary send failure (state-operation-contract.md 18) */
        return;
    }

    s_op_state           = SMS_OP_SENDING; /* retry from the top - advance_send() will call EC200U_SMS_Send() again once the backoff below elapses */
    s_retry_pending      = true;
    s_next_attempt_tick  = Time_GetTick();
}

void SMS_Init(void) {
    s_op_state      = SMS_OP_IDLE;
    s_retry_count   = 0;
    s_retry_pending = false;
    s_next_event_id = 1;
    s_send_throttled = false;
    s_last_send_tick = 0;
    memset(&s_current_send, 0, sizeof(s_current_send));
    memset(s_current_destination, 0, sizeof(s_current_destination));
    memset(s_current_payload, 0, sizeof(s_current_payload));
}

void SMS_Process(void) {
    process_incoming();

    if (s_op_state == SMS_OP_IDLE) {
        try_start_next();
    }
    advance_send();
    advance_waiting_result();
}

bool SMS_SubmitRequest(const SmsRequest* request) {
    if (!request) {
        return false;
    }

    SmsQueueItem item;
    item.kind            = SMS_QUEUE_ITEM_REQUEST;
    item.payload.request = *request;
    return Queue_Push(Queues_GetSmsRequestQueue(), &item, sizeof(item));
}
