#include "managements/call_management.h"
#include "managements/call_management_config.h"
#include "infrastructure/configuration.h"
#include "managements/power_management.h"
#include "infrastructure/ec200u_manager.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"
#include "shared/reset_request.h"
#include <string.h>

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * SECURITY / PRIVACY - REMOTE CABIN LISTEN-IN RISK - CLOSED AT THIS LAYER
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * Background (why this exists): docs/00-project/rfp-saipa-connected-car.md
 * line 103 explicitly lists "قابلیت شنود: سفارشی - در صورت نیاز مالک"
 * (listen-in capability: custom, if the owner needs it) as a requested
 * feature - meaning cabin listen-in was always meant to be an intentional,
 * gated, owner-authorized capability, never an accidental default-on one.
 * Previously, this Management had no Answer() step and no Caller ID check
 * anywhere: the incoming-call branch in poll_and_advance() (case
 * CALL_IDLE) accepted ANY call the modem reported as RINGING/ACTIVE as a
 * legitimate incoming call, without ever asking "who is calling".
 *
 * Current behavior (per explicit user instruction, this session): every
 * incoming call is now checked against the Allowed Phone Numbers list
 * (infrastructure/configuration.h's DeviceConfig.access, via
 * Call_IsNumberAllowed()) BEFORE it is ever answered. Call Management owns
 * the *meaning* of this list (docs/03-infrastructure-contracts/
 * configuration-infrastructure.md section 9); Configuration Infrastructure
 * only stores/persists it, starting from a PLACEHOLDER factory default
 * (one owner number - see LoadFactoryDefaults() in
 * infrastructure/configuration.c) until something calls
 * Configuration_Update(CONFIG_FIELD_ALLOWED_PHONE_NUMBERS, ...) with the
 * real list. An empty list (count == 0, e.g. after a corrupted read)
 * still means Fail-Closed - nobody gets through:
 *   - Normal path (modem reports EC200U_CALL_RINGING, not yet answered):
 *     Call Management reads the caller's number with
 *     EC200U_Call_GetIncomingNumber() and only calls EC200U_Call_Answer()
 *     if that number is on the allowlist. Anything else - a disallowed
 *     number, or GetIncomingNumber() failing to report one at all - is
 *     rejected Fail-Closed: EC200U_Call_Hangup() is called instead, and
 *     the call is NEVER answered.
 *   - Fallback path (modem reports EC200U_CALL_ACTIVE directly, with no
 *     RINGING observed first): this means the EC200U module itself
 *     auto-answered the call before Call Management ever got a chance to
 *     decide - see the assumption paragraph below. There is no "reject
 *     before answering" option left at that point, so the only remaining
 *     protective action is to hang up immediately if the (now already
 *     connected) caller cannot be confirmed as allowed.
 *
 * IMPORTANT REMAINING ASSUMPTION: the normal path above only actually
 * prevents an unauthorized connection if the EC200U module is configured
 * (at the AT-command level, e.g. auto-answer / ATS0) to WAIT for an
 * explicit Answer rather than answering incoming calls on its own. This is
 * a Firmware-level architectural assumption, not something this
 * Management (or this Contract) can see, control, or guarantee from here -
 * it depends entirely on the real modem/module configuration, which is
 * still owned by the (not yet delivered) EC200U contractor component
 * (docs/05-outsourcing/ec200u-component-outsourcing-spec.md). If that
 * assumption turns out to be false on real hardware, every incoming call
 * would arrive already EC200U_CALL_ACTIVE - the fallback path above still
 * catches and hangs up any unauthorized one, but only AFTER it was
 * already connected for at least one Call_Process() cycle, not before.
 * This assumption must be verified once the contractor's real Component
 * is integrated. There is also still no local indication (LED/buzzer) of
 * an incoming call being accepted or rejected. */

/* Full contract: docs/02-management-contracts/call-management.md
 * State/Operation pattern: docs/02-management-contracts/state-operation-contract.md
 *
 * Call Management only ever talks to the EC200U Manager Voice Call
 * Capability Contract (infrastructure/ec200u_manager.h) - never AT
 * Command/modem protocol detail. That Contract is still a draft
 * (OPEN-ITEMS.md, red, #1), so only the functions already exposed there
 * are used here: EC200U_Call_Dial(), EC200U_Call_Hangup(),
 * EC200U_Call_GetState().
 *
 * One active call at a time (state-operation-contract.md section 15,
 * "One active operation"): a new CallRequest submitted while one is
 * already in progress simply waits in the Call Request Queue
 * (Queues_GetCallRequestQueue()) until the current one reaches
 * CALL_IDLE again - same "the queue itself is the QUEUED policy" choice
 * SMS Management makes (sms_management.c).
 *
 * Single FSM variable, not a separate internal/public split: unlike the
 * "WAIT_AT_RESPONSE / PARSE_RESPONSE / ..." example in state-operation-
 * contract.md section 12, EC200U_CallState (infrastructure/
 * ec200u_manager.h) already gives Call Management a clean, five-value,
 * protocol-free view of the modem (IDLE/DIALING/RINGING/ACTIVE/ENDED)
 * that maps almost 1:1 onto this Management's own public CallState
 * (IDLE/OUTGOING/RINGING/CONNECTED/ENDING) - any AT-command-level detail
 * beneath that already lives inside EC200U Manager, out of this
 * Management's reach. Introducing a second, separate internal-only
 * state enum on top would only duplicate s_public_state without adding
 * any real information, so s_public_state below is used directly as the
 * one and only FSM driver.
 */

/* --- Incoming vs. outgoing call detection (contract section 8) ---
 * EC200U_CallState carries no direction field - RINGING/ACTIVE look
 * identical whether the network is ringing us or we are ringing someone
 * else. The only non-guessed way to tell them apart is for Call
 * Management to remember, for the current/pending session, whether IT
 * was the one that called EC200U_Call_Dial(): if EC200U_Call_GetState()
 * is ever observed moving out of EC200U_CALL_IDLE while Call Management
 * itself never issued a Dial for this session (s_public_state was still
 * CALL_IDLE), that can only be an incoming call. */
typedef enum {
    CALL_ORIGIN_NONE,     /* no session in progress - only valid while s_public_state == CALL_IDLE */
    CALL_ORIGIN_OUTGOING, /* Call Management itself called EC200U_Call_Dial() for the current session */
    CALL_ORIGIN_INCOMING  /* detected per the paragraph above - Call Management never dialed this one */
} CallOrigin;

/* CallQueueItem (the tagged item actually placed on
 * Queues_GetCallRequestQueue()) is declared in call_management.h, not
 * here, so that shared/event_policy.c can build one directly for a
 * Policy-originated call request - exactly the same arrangement as
 * SMS Management's SmsQueueItem (sms_management.h), which
 * shared/event_policy.c already does for the SMS Request Queue. */

/* TODO(open-item): the system-wide Fault struct is still open
 * (OPEN-ITEMS.md red #5). Local/provisional shape, same pattern already
 * used for SmsFaultRecord (sms_management.c) and GpsTelemetryRecord
 * (gps_management.c) for their own still-open Contracts. */
typedef struct {
    uint32_t fault_code;
    uint32_t timestamp;
} CallFaultRecord;

static CallState  s_public_state;
static CallOrigin s_origin;
static char       s_current_destination[CALL_MAX_DESTINATION_LENGTH];

/* Retry counter for the CURRENT outgoing CallRequest only - reset every
 * time a new one is pulled off the Queue (try_start_next()/start_dial()). */
static uint32_t s_dial_attempts;

/* Cross-request failure counter driving the Reset Request escalation
 * (contract section 11) - persists across CallRequests, reset only when
 * a call genuinely reaches CONNECTED (enter_connected()). Same shape as
 * gps_management.c's s_recovery_attempts. */
static uint32_t s_consecutive_failures;
static bool     s_reset_request_sent; /* latch: at most one Reset Request per unresponsive episode, like gps_management.c */

/* Generic "reference point in time for the currently active State" used
 * by both the outgoing no-connection timeout (CALL_OUTGOING/CALL_RINGING)
 * and the termination timeout (CALL_ENDING) - only one of those two is
 * ever active at a time, so a single field is enough. Always rewritten
 * unconditionally whenever an attempt is (re)issued, regardless of that
 * attempt's own immediate result - this is the exact lesson from the
 * gps_management.c regression fix (see check_fault_and_recover()'s
 * comment there): if this were only refreshed on a *successful* EC200U
 * call, a genuinely unresponsive module would never advance it, and
 * every subsequent Call_Process() call (even back-to-back within the
 * same Super Loop pass) would re-enter the timeout branch immediately -
 * collapsing what is meant to be CALL_MAX_DIAL_ATTEMPTS attempts spaced
 * a full CALL_NO_CONNECTION_TIMEOUT_MS apart into a handful of
 * back-to-back retries, and escalating to a Reset Request far earlier
 * than designed. */
static TimeTick s_state_deadline_reference_tick;

static bool s_terminate_requested;

/* Background incoming-call poll throttle (contract section 13, Power
 * Mode) - only consulted while s_public_state == CALL_IDLE. */
static bool     s_idle_poll_started;
static TimeTick s_last_idle_poll_tick;

static uint32_t s_next_event_id;

static void emit_event(EventType type) {
    Event e;
    e.event_type = type;
    e.event_id   = s_next_event_id++;
    e.timestamp  = Time_GetTick();
    (void)Queue_Push(Queues_GetEventQueue(), &e, sizeof(e));
}

static void push_fault(uint32_t fault_code) {
    CallFaultRecord fault;
    fault.fault_code = fault_code;
    fault.timestamp  = Time_GetTick();
    (void)Queue_Push(Queues_GetFaultQueue(), &fault, sizeof(fault));
}

/* strlen() would be unsafe here for the same reason sms_management.c's
 * bounded_strlen() exists: a caller-supplied or queued destination
 * buffer is not guaranteed to be NUL-terminated within its declared
 * size. Duplicated locally rather than shared, same as SMS/GPS's own
 * small local helpers - no shared string-utility module exists yet. */
static uint32_t bounded_strlen(const char* s, uint32_t max) {
    uint32_t len = 0;
    while (len < max && s[len] != '\0') {
        len++;
    }
    return len;
}

/* Call_IsNumberAllowed() is declared in call_management.h and defined
 * further down (near the other public functions), not here - see its
 * comment there. It IS called from poll_and_advance()'s CALL_IDLE case
 * below, as the Fail-Closed gate on every incoming call. */

/* Local Recovery escalation to a Reset Request - contract section 11
 * ("ابتدا Recovery محلی ... در صورت نیاز ... Reset Request"). Called
 * only after this Management has already given up locally on a specific
 * Operation (dial attempts exhausted, or a termination timeout) - a
 * single ordinary Operation Failure is never by itself a System Fault
 * (state-operation-contract.md section 18). */
static void note_operation_failure(void) {
    ResetRequest req;

    s_consecutive_failures++;
    if (s_consecutive_failures >= CALL_MAX_CONSECUTIVE_OPERATION_FAILURES && !s_reset_request_sent) {
        req.type      = RESET_TYPE_MODULE_RESET;
        req.target    = "EC200U";
        req.source    = RESET_SOURCE_CALL;
        req.reason    = RESET_REASON_MODULE_UNRESPONSIVE;
        req.timestamp = Time_GetTick();
        System_SubmitResetRequest(&req);
        s_reset_request_sent = true; /* do not resubmit every cycle while still unresponsive */
    }
}

static void reset_to_idle(void) {
    s_public_state  = CALL_IDLE;
    s_origin        = CALL_ORIGIN_NONE;
    s_dial_attempts = 0;
    memset(s_current_destination, 0, sizeof(s_current_destination));
}

/* A call genuinely reached CONNECTED - proof the Dial/Ringing/Active
 * path through EC200U actually works end-to-end right now, regardless of
 * which direction this particular call was. Clears both the per-request
 * retry counter's parent (cross-request) failure counter and re-arms the
 * Reset Request latch for any future episode - same reasoning as
 * gps_management.c's poll_position() on its EC200U_OK path. */
static void enter_connected(void) {
    s_public_state          = CALL_CONNECTED;
    s_consecutive_failures  = 0;
    s_reset_request_sent    = false;
}

/* Begins the ENDING state - either because Call_Terminate() was called
 * (contract section 9, "Terminate Request"), or because this Management
 * gave up locally on a stuck outgoing attempt and is cleaning up before
 * returning to IDLE. Hangup's own Result is intentionally ignored here,
 * for the same reason start_dial()/retry ignore EC200U_Call_Dial()'s
 * Result: handle_ending()'s own timeout is the real safety net if this
 * silently never takes effect. */
static void enter_ending(void) {
    (void)EC200U_Call_Hangup();
    s_public_state                   = CALL_ENDING;
    s_state_deadline_reference_tick  = Time_GetTick();
}

/* Called when an outgoing attempt did not result in a connected call -
 * either CALL_NO_CONNECTION_TIMEOUT_MS elapsed while still not
 * CONNECTED, or the modem itself already reports IDLE/ENDED for an
 * attempt that never reached ACTIVE (contract section 10: "Call Request
 * -> No Connection -> Timeout -> Retry / Failure"). Both are treated
 * identically: this specific attempt did not produce a connected call. */
static void handle_dial_setup_failure(void) {
    /* Same "increment then compare with >" shape as sms_management.c's
     * advance_send(): CALL_MAX_DIAL_ATTEMPTS counts RETRIES, on top of
     * the one initial Dial already issued by start_dial() - so the total
     * number of Dial attempts for one CallRequest before giving up is
     * 1 + CALL_MAX_DIAL_ATTEMPTS. */
    s_dial_attempts++;

    if (s_dial_attempts <= CALL_MAX_DIAL_ATTEMPTS) {
        (void)EC200U_Call_Dial(s_current_destination); /* retry - Result ignored, see start_dial()'s comment */
        s_state_deadline_reference_tick = Time_GetTick(); /* unconditional - see s_state_deadline_reference_tick's comment */
        s_public_state = CALL_OUTGOING; /* back to "just (re)dialed, not yet ringing" for this fresh attempt */
        return;
    }

    /* Retries exhausted for this CallRequest - give up on it (an ordinary
     * Operation Failure, not automatically a System Fault -
     * state-operation-contract.md section 18) and free the "one active
     * operation" slot for the next queued CallRequest, if any. */
    push_fault(CALL_FAULT_CODE_SETUP_FAILED);
    note_operation_failure();
    reset_to_idle();
}

/* Advances the CALL_ENDING State by one step per Process() cycle. */
static void handle_ending(EC200U_CallState modem_state) {
    if (modem_state == EC200U_CALL_IDLE || modem_state == EC200U_CALL_ENDED) {
        reset_to_idle();
        return;
    }

    if (Time_Elapsed(s_state_deadline_reference_tick, CALL_HANGUP_TIMEOUT_MS)) {
        /* Local Recovery: cannot block Call_Process() waiting forever for
         * the modem to confirm (state-operation-contract.md section 1,
         * Non-Blocking) - force our own public State back to CALL_IDLE
         * regardless, and count this as an Operation Failure (contract
         * section 11, "Call Termination Failure") toward the Reset
         * Request escalation. No further Hangup retry is attempted here
         * on purpose - the Contract leaves Retry as optional Operation
         * Policy (section 10/14), and retrying a Hangup whose Result is
         * never even inspected would not add any real information beyond
         * what this timeout-based fallback already provides. */
        push_fault(CALL_FAULT_CODE_TERMINATION_FAILED);
        note_operation_failure();
        reset_to_idle();
    }
}

/* Issues the very first Dial for a freshly-pulled CallRequest (as
 * opposed to handle_dial_setup_failure()'s retry of the same one). */
static void start_dial(const char* destination) {
    uint32_t len = bounded_strlen(destination, CALL_MAX_DESTINATION_LENGTH - 1u);
    memcpy(s_current_destination, destination, len);
    s_current_destination[len] = '\0';

    s_origin        = CALL_ORIGIN_OUTGOING;
    s_dial_attempts = 0;
    s_public_state  = CALL_OUTGOING;

    /* Result intentionally ignored, exactly like gps_management.c's
     * ensure_gnss_started() ignores EC200U_GNSS_Start()'s Result: a
     * rejected Dial (BUSY/NOT_READY/ERROR) is handled identically to a
     * Dial that was accepted but silently never answered - both simply
     * fail to make progress and are caught by the same
     * CALL_NO_CONNECTION_TIMEOUT_MS timeout/retry loop below. */
    (void)EC200U_Call_Dial(s_current_destination);
    s_state_deadline_reference_tick = Time_GetTick();
}

/* Step 1 (only while fully IDLE - state-operation-contract.md section
 * 15, "One active operation"): pull the next queued CallRequest, if any,
 * and start dialing it. See CallQueueItem's comment in call_management.h
 * for why the two producer kinds need a tag.
 *
 * Returns true iff a fresh Dial was actually issued this cycle - Call_Process()
 * uses this to skip poll_and_advance() for the same cycle a Dial was just
 * issued (see its own comment for why). */
static bool try_start_next(void) {
    CallQueueItem item;
    uint32_t      len;
    const char*   destination;

    if (s_public_state != CALL_IDLE) {
        return false;
    }

    if (!Queue_Pop(Queues_GetCallRequestQueue(), &item, sizeof(item), &len)) {
        return false;
    }

    if (item.kind == CALL_QUEUE_ITEM_ALERT) {
        /* Bug fix: this used to be a compile-time CALL_ALERT_DESTINATION_PLACEHOLDER,
         * never reconfigurable at runtime, unlike sms_management.c's
         * analogous alert path (which already reads
         * Configuration_Get()->access.sms_alert_destination). The
         * Configuration field/setter for this exact purpose
         * (DeviceConfig.access.call_alert_destination,
         * Configuration_SetCallAlertDestination()) already existed and was
         * simply never read here - see configuration.h's ConfigAccess
         * comment. */
        destination = Configuration_Get()->access.call_alert_destination;
    } else {
        item.payload.request.destination[CALL_MAX_DESTINATION_LENGTH - 1u] = '\0'; /* defensive - guarantee NUL-termination regardless of the producer */
        destination = item.payload.request.destination;
    }

    start_dial(destination);
    return true;
}

/* Step 2: one non-blocking poll of EC200U_Call_GetState() plus whatever
 * State transition/timeout it implies - contract section 7 ("Process()
 * -> Check Call Status -> Continue -> Return", never waiting inline for
 * Ringing/Connection/Remote Answer/Termination). */
static void poll_and_advance(void) {
    EC200U_CallState modem_state;
    bool             background_only = (s_public_state == CALL_IDLE);

    /* Contract section 13 (Power Mode): the ONLY activity allowed to slow
     * down here is background polling while nothing is happening at all.
     * The moment a call is in progress (any State other than CALL_IDLE),
     * every Process() cycle below polls unconditionally. */
    if (background_only) {
        uint32_t interval = CALL_IDLE_POLL_INTERVAL_MS;
        if (Power_GetMode() == POWER_LOW_POWER) {
            interval *= CALL_LOW_POWER_POLL_RATE_MULTIPLIER;
        }
        if (s_idle_poll_started && !Time_Elapsed(s_last_idle_poll_tick, interval)) {
            return; /* still within the throttled idle poll interval - nothing else to do */
        }
        s_idle_poll_started    = true;
        s_last_idle_poll_tick  = Time_GetTick();
    }

    /* Contract section 9 ("Terminate Request"): can cut off a call at any
     * stage (OUTGOING/RINGING/CONNECTED), not only once CONNECTED - a
     * pending outgoing setup can be cancelled too (state-operation-
     * contract.md section 6, PENDING/RUNNING -> CANCELLED). A request
     * with nothing left to terminate (already IDLE, or already ENDING)
     * is simply discarded here. */
    if (s_terminate_requested) {
        s_terminate_requested = false;
        if (s_public_state != CALL_IDLE && s_public_state != CALL_ENDING) {
            enter_ending();
            return;
        }
    }

    modem_state = EC200U_Call_GetState();

    switch (s_public_state) {
        case CALL_IDLE: {
            char incoming_number[CALL_MAX_DESTINATION_LENGTH];
            bool number_ok;

            if (modem_state != EC200U_CALL_RINGING && modem_state != EC200U_CALL_ACTIVE) {
                break;
            }

            /* Never dialed for this session while sitting IDLE - contract
             * section 8, the only non-guessed way to detect an incoming
             * call (see CallOrigin's comment above). Always reported via
             * EVENT_INCOMING_CALL regardless of whether it ends up
             * accepted or rejected below - the Event only means "an
             * incoming call happened", not "it was answered". */
            emit_event(EVENT_INCOMING_CALL);

            /* SECURITY (see the warning block at the top of this file):
             * Fail-Closed Caller ID check. Only a number confirmed on the
             * allowlist may ever be connected; EC200U_Call_GetIncomingNumber()
             * returning anything other than EC200U_OK ("cannot verify who
             * this is") is rejected exactly like an explicitly disallowed
             * number - never treated as an implicit allow. */
            number_ok = (EC200U_Call_GetIncomingNumber(incoming_number, sizeof(incoming_number)) == EC200U_OK)
                        && Call_IsNumberAllowed(incoming_number);

            if (modem_state == EC200U_CALL_RINGING) {
                if (number_ok) {
                    /* Result intentionally ignored, same convention as
                     * every other EC200U_Call_*() call in this file
                     * (start_dial(), enter_ending()): a rejected/failed
                     * Answer simply fails to make progress and is caught
                     * by the normal CALL_RINGING machinery below, not by
                     * inspecting the Result here. */
                    (void)EC200U_Call_Answer();
                    s_origin        = CALL_ORIGIN_INCOMING;
                    s_public_state  = CALL_RINGING; /* not CONNECTED yet - the next Process() cycle must observe EC200U_CALL_ACTIVE for itself through the normal CALL_RINGING branch below, one step at a time */
                } else {
                    /* Reject BEFORE ever answering - the safe, normal case
                     * this allowlist exists for. Actively hang up rather
                     * than merely ignoring the ring, and never leave
                     * CALL_IDLE - nothing was ever answered, so there is
                     * nothing to tear down via CALL_ENDING. This is not a
                     * Fault: refusing an unauthorized/unverifiable caller
                     * is expected, correct behavior, not a system
                     * malfunction (no push_fault()/note_operation_failure()). */
                    (void)EC200U_Call_Hangup();
                }
            } else { /* EC200U_CALL_ACTIVE - modem connected on its own, see the SECURITY warning at the top of this file */
                if (number_ok) {
                    /* Already connected and now confirmed allowed - accept
                     * it as an incoming call that happened to already be
                     * answered, same as this branch's previous
                     * (pre-allowlist) behavior. */
                    s_origin = CALL_ORIGIN_INCOMING;
                    enter_connected();
                } else {
                    /* Already connected but NOT confirmed allowed - the
                     * "reject before Answer" option no longer exists (the
                     * modem already answered on its own), so the only
                     * protective action left is to hang up immediately.
                     * Not a Fault, same reasoning as the RINGING-reject
                     * case above. */
                    (void)EC200U_Call_Hangup();
                }
            }
            break;
        }

        case CALL_OUTGOING:
        case CALL_RINGING:
            if (modem_state == EC200U_CALL_ACTIVE) {
                enter_connected();
            } else if (modem_state == EC200U_CALL_RINGING && s_public_state == CALL_OUTGOING) {
                /* Progressed past setup, still not connected - the
                 * no-connection timeout below keeps counting from the
                 * original Dial rather than resetting here, so the total
                 * time budget for one attempt stays bounded by
                 * CALL_NO_CONNECTION_TIMEOUT_MS regardless of how much of
                 * it was spent dialing vs. ringing. */
                s_public_state = CALL_RINGING;
            } else if (s_origin == CALL_ORIGIN_OUTGOING) {
                /* Anything else here (still DIALING, or already back to
                 * IDLE/ENDED) is only conclusive once a full
                 * CALL_NO_CONNECTION_TIMEOUT_MS window has passed -
                 * deliberately NOT reacting immediately to a single
                 * IDLE/ENDED reading. This is the exact
                 * gps_management.c regression lesson (see
                 * s_state_deadline_reference_tick's comment above):
                 * reacting to the raw state right away, instead of only
                 * to an elapsed timeout, would let a modem that is stuck
                 * reporting IDLE/ENDED re-trigger
                 * handle_dial_setup_failure() on every single
                 * consecutive Process() call with zero elapsed time,
                 * collapsing every one of CALL_MAX_DIAL_ATTEMPTS retries
                 * - meant to be spaced a full timeout window apart -
                 * into one immediate burst. */
                if (Time_Elapsed(s_state_deadline_reference_tick, CALL_NO_CONNECTION_TIMEOUT_MS)) {
                    handle_dial_setup_failure();
                }
            } else if (modem_state == EC200U_CALL_IDLE || modem_state == EC200U_CALL_ENDED) {
                /* CALL_ORIGIN_INCOMING: rejected/rang off before we
                 * answered - a normal, immediate outcome for a call we
                 * never placed ourselves (contract section 9, natural
                 * end). There is no "did our own Dial silently fail"
                 * ambiguity to wait out for an incoming call, so this
                 * does not need to be timeout-gated like the outgoing
                 * case above. */
                reset_to_idle();
            }
            /* else: an incoming call still RINGING - no timeout is applied
             * here on purpose. Deciding how long to let an incoming call
             * ring before doing something about it is Policy, not this
             * Capability (contract section 12/16); Call Management only
             * reports the State and waits. */
            break;

        case CALL_CONNECTED:
            if (modem_state == EC200U_CALL_IDLE || modem_state == EC200U_CALL_ENDED) {
                /* Natural end - contract section 9: "CONNECTED -> Remote
                 * Hangup -> Call Ended". Nothing to send to EC200U here
                 * (it already settled the call on its own); CALL_ENDING is
                 * just a one-cycle waypoint back to CALL_IDLE, handled
                 * uniformly by handle_ending() below regardless of
                 * whether this call ended naturally or via our own
                 * Hangup. */
                s_public_state                   = CALL_ENDING;
                s_state_deadline_reference_tick  = Time_GetTick();
            }
            /* else: still an active conversation - Call Management does
             * not impose any maximum call duration; that would be Policy,
             * not this Capability (contract section 12/16). */
            break;

        case CALL_ENDING:
            handle_ending(modem_state);
            break;
    }
}

void Call_Init(void) {
    s_public_state = CALL_IDLE;
    s_origin       = CALL_ORIGIN_NONE;
    memset(s_current_destination, 0, sizeof(s_current_destination));

    s_dial_attempts         = 0;
    s_consecutive_failures  = 0;
    s_reset_request_sent    = false;

    s_state_deadline_reference_tick = 0;

    s_terminate_requested = false;

    s_idle_poll_started   = false;
    s_last_idle_poll_tick = 0;

    s_next_event_id = 1;
}

void Call_Process(void) {
    /* If a fresh CallRequest was just picked up and Dial issued this very
     * cycle, poll_and_advance() is skipped for this same cycle on
     * purpose: the modem has not had a chance to act on the Dial yet, so
     * EC200U_Call_GetState() would still read whatever it reported
     * beforehand (typically EC200U_CALL_IDLE) - treating that as "no
     * connection" immediately, before the attempt even had one full
     * Process() cycle to register, would be misinterpreting a stale
     * reading rather than a real failure. The very next Call_Process()
     * cycle polls normally. */
    if (try_start_next()) {
        return;
    }
    poll_and_advance();
}

bool Call_SubmitRequest(const CallRequest* request) {
    CallQueueItem item;
    uint32_t      len;

    if (!request || !request->destination || request->destination[0] == '\0') {
        return false;
    }

    memset(&item, 0, sizeof(item));
    item.kind = CALL_QUEUE_ITEM_REQUEST;
    len = bounded_strlen(request->destination, CALL_MAX_DESTINATION_LENGTH - 1u);
    memcpy(item.payload.request.destination, request->destination, len);
    item.payload.request.destination[len] = '\0';

    return Queue_Push(Queues_GetCallRequestQueue(), &item, sizeof(item));
}

bool Call_Terminate(void) {
    if (s_public_state == CALL_IDLE) {
        return false; /* nothing active to terminate */
    }
    s_terminate_requested = true;
    return true;
}

CallState Call_GetState(void) {
    return s_public_state;
}

/* SECURITY-SENSITIVE - see the warning block at the top of this file.
 * Checks a phone number against DeviceConfig.access.allowed_phone_numbers
 * (infrastructure/configuration.h). Public (not static) and exposed in
 * call_management.h so it can be unit-tested directly, in addition to
 * being called from poll_and_advance()'s CALL_IDLE case above, which is
 * the actual enforcement point: every incoming call (RINGING or ACTIVE)
 * is checked against this list before it is ever answered/accepted.
 * Configuration Infrastructure starts this list at a PLACEHOLDER factory
 * default (LoadFactoryDefaults(), infrastructure/configuration.c) until
 * it is field-updated via Configuration_Update() with the real list - see
 * docs/03-infrastructure-contracts/configuration-infrastructure.md
 * section 7 for who is expected to call that (Server Management/API, or a
 * Provisioning tool), which is not this Management's own responsibility. */
bool Call_IsNumberAllowed(const char* number) {
    if (!number) {
        return false;
    }

    const ConfigAllowedPhoneNumbers* allowed = &Configuration_Get()->access.allowed_phone_numbers;
    for (uint32_t i = 0; i < allowed->count; i++) {
        if (strcmp(number, allowed->numbers[i].number) == 0) {
            return true;
        }
    }
    return false;
}
