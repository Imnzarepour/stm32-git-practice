#include "managements/diagnostic_management.h"
#include "managements/diagnostic_management_config.h"
#include "managements/power_management.h"
#include "hardware_proxy/hardware_proxy_can.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"
#include "shared/reset_request.h"
#include <stddef.h>

/* Contract source: docs/02-management-contracts/diagnostic-management.md
 *
 * What this file IS: the Current+Pending lifecycle machinery (sections
 * 5-13), the ~2/minute periodic Read Parameter scheduler (section 4), the
 * LOW_POWER policy for it (section 21 leaves this Management's own
 * behavior per Mode up to this Contract - decision made here: the
 * automatic periodic read is suppressed in LOW_POWER, same discipline
 * hardware_input_management.c already uses for its own Telemetry rate;
 * an explicit request from another Management is never blocked by Power
 * Mode, since Main Firmware, not this file, judges whether that request
 * was worth making right now), section 25's Diagnostic -> Event/Fault ->
 * Event Policy path (see fail_current()) plus the internal periodic
 * read's own result lifecycle (see s_current_is_periodic below and its
 * own comment - this closes a real bug where the very first periodic Read
 * Parameter permanently starved Current/Pending for the rest of the
 * device's uptime, because nothing external was ever polling for ITS
 * result), and section 22's Reset escalation (see
 * maybe_escalate_to_reset() - past DIAG_MAX_CONSECUTIVE_FAILURES
 * consecutive Operation failures, a Reset Request is submitted to System
 * Management instead of silently reporting FAILED forever).
 *
 * What this file is NOT: a real CAN/UDS/K-Line Protocol implementation.
 * build_request_frame()/succeed_current() below are a deliberately thin
 * placeholder - CLAUDE.md is explicit that the real Diagnostic Engine
 * Protocol is the separate Diagnostic team's responsibility, not
 * something to guess here. They exist only so the Non-Blocking Send ->
 * Receive -> Timeout exchange (the actual architectural Contract this
 * file owns) is real and testable today through Hardware Proxy, not
 * mocked away entirely. Replace them once the Diagnostic team's real
 * framing is defined; nothing in the Current+Pending state machine above
 * them needs to change when that happens. */

typedef enum {
    DIAG_SLOT_IDLE,
    DIAG_SLOT_RUNNING,
    DIAG_SLOT_SUCCESS,
    DIAG_SLOT_FAILED
} DiagSlotState;

static DiagSlotState      s_current_state;
static DiagnosticRequest  s_current_request;
static DiagnosticResponse s_current_response;
static uint32_t           s_current_started_tick;
static bool                s_current_request_sent; /* whether the CAN request for this attempt has been sent yet */

/* Set by start_operation()'s `is_periodic` argument. true only when Current
 * was started by maybe_start_periodic_read() below, never for a request
 * that came in through the public Diagnostic_SubmitRequest() (directly, or
 * promoted out of Pending - Pending is only ever populated by
 * Diagnostic_SubmitRequest() itself, see that function). Exists to fix a
 * real bug: nothing external ever calls Diagnostic_GetResponse() for the
 * internal periodic read (only server_management.c does, and only while
 * s_diagnostic_command_in_flight tracks a Server-originated Request it
 * itself submitted) - so before this flag existed, the very first periodic
 * Read Parameter's SUCCESS/FAILED result sat in Current forever (section
 * 8's "unconsumed Result still counts as occupied"), which (a) permanently
 * stopped maybe_start_periodic_read() from ever running again (it only
 * starts from IDLE), and (b) worse, caused the NEXT external Diagnostic
 * Request (queued into Pending because Current was "occupied") to be
 * mis-reported: whoever next called Diagnostic_GetResponse() (external
 * side) would receive the STALE periodic result, attributed to their own request,
 * while their real request's actual result would then get stuck the exact
 * same way. See Diagnostic_GetResponse() and Diagnostic_Process()'s
 * periodic-consume branch below for the fix - Current started by the
 * periodic scheduler is now entirely private to this file: never handed
 * out via Diagnostic_GetResponse(), and freed by this file itself as soon
 * as it completes. */
static bool                s_current_is_periodic;

static bool               s_pending_present;
static DiagnosticRequest  s_pending_request;

static uint32_t           s_last_periodic_tick;

/* Section 22 Reset escalation ("در صورت نیاز به Recovery سطح Module/
 * System"): counts consecutive Operation failures across ALL Operations
 * (periodic and explicit alike - a broken CAN bus is a System-level
 * problem no matter who asked), reset on any success. Same
 * count-then-escalate-then-latch shape gps_management.c's
 * s_recovery_attempts/s_reset_request_sent already use for its own
 * Module Reset escalation. */
static uint32_t           s_consecutive_failures;
static bool                s_reset_request_sent; /* latch: at most one Reset Request per unresponsive episode */

/* TODO(open-item): the system-wide Fault struct is still open (OPEN-ITEMS.md
 * red #5). This is a local, provisional shape for Diagnostic Operation
 * failures only, same pattern already used by ActuatorFaultRecord
 * (actuator_management.c)/SmsFaultRecord/CallFaultRecord for their own
 * still-open Contracts - not the final Fault Contract. */
#define DIAG_FAULT_CODE_OPERATION_FAILED 6001u

typedef struct {
    uint32_t fault_code;
    uint32_t operation;   /* the DiagnosticOperation that failed */
    uint32_t status;      /* the DiagnosticStatus it failed with (BUS_ERROR/TIMEOUT/...) */
    uint32_t timestamp;
} DiagnosticFaultRecord;

/* diagnostic-management.md section 25: "Diagnostic -> Event/Fault (مثل
 * DIAGNOSTIC_FAILURE, ECU_UNAVAILABLE) -> Event Policy". Diagnostic
 * Management itself never runs Event Policy (routing to SMS/Call/Server/
 * Log is entirely that Component's call, see shared/event_policy.c) - it
 * only ever produces the raw signal, same "Detection -> Event -> Policy"
 * boundary every other Event-producing Management in this firmware already
 * follows (e.g. actuator_management.c's report_output_failure()). Called
 * from fail_current() below, so it fires for every Operation that ends
 * FAILED regardless of who submitted it (periodic or explicit) - the
 * Contract does not distinguish origin here, unlike the Response
 * consumption rules s_current_is_periodic enforces above. */
static void report_operation_failure(DiagnosticOperation operation, DiagnosticStatus status, uint32_t timestamp) {
    DiagnosticFaultRecord fault;
    fault.fault_code = DIAG_FAULT_CODE_OPERATION_FAILED;
    fault.operation   = (uint32_t)operation;
    fault.status      = (uint32_t)status;
    fault.timestamp   = timestamp;
    (void)Queue_Push(Queues_GetFaultQueue(), &fault, sizeof(fault));

    Event event;
    event.event_type = EVENT_DIAGNOSTIC_FAILURE;
    event.event_id    = 0u; /* not used anywhere yet - same placeholder as every other Event producer in this firmware (e.g. actuator_management.c's push_event()) */
    event.timestamp   = timestamp;
    (void)Queue_Push(Queues_GetEventQueue(), &event, sizeof(event));
}

static void start_operation(const DiagnosticRequest* request, bool is_periodic) {
    s_current_request       = *request;
    s_current_state         = DIAG_SLOT_RUNNING;
    s_current_request_sent  = false;
    s_current_started_tick  = Time_GetTick();
    s_current_is_periodic   = is_periodic;
}

/* Section 22: past DIAG_MAX_CONSECUTIVE_FAILURES, local per-Operation
 * recovery (a Response the caller can retry) is no longer enough - this
 * Management gives up locally and asks System Management to intervene.
 * CAN is an MCU-internal peripheral (Hardware Proxy CAN domain), not an
 * external Module like EC200U, so this is a SYSTEM_RESET (whole MCU),
 * same target class actuator_management.c's own report_output_failure()
 * already uses for its own MCU-internal peripheral (GPIO) - never a
 * MODULE_RESET, which reset-request-contract.md only defines for EC200U. */
static void maybe_escalate_to_reset(uint32_t timestamp) {
    s_consecutive_failures++;
    if (s_consecutive_failures < DIAG_MAX_CONSECUTIVE_FAILURES || s_reset_request_sent) {
        return;
    }

    ResetRequest reset_request;
    reset_request.type      = RESET_TYPE_SYSTEM_RESET;
    reset_request.target    = "MCU";
    reset_request.source    = RESET_SOURCE_DIAGNOSTIC;
    reset_request.reason    = RESET_REASON_COMMUNICATION_STUCK;
    reset_request.timestamp = timestamp;
    System_SubmitResetRequest(&reset_request);
    s_reset_request_sent = true; /* do not resubmit every cycle while still failing */
}

static void fail_current(DiagnosticStatus status) {
    s_current_response.status       = status;
    s_current_response.data_length  = 0u;
    s_current_state                 = DIAG_SLOT_FAILED;
    report_operation_failure(s_current_request.operation, status, Time_GetTick());
    maybe_escalate_to_reset(Time_GetTick());
}

static void succeed_current(const HWProxy_CanFrame* response_frame) {
    uint32_t copy_length = response_frame->dlc;
    if (copy_length > sizeof(s_current_response.data)) {
        copy_length = (uint32_t)sizeof(s_current_response.data);
    }

    s_current_response.status       = DIAG_STATUS_SUCCESS;
    s_current_response.data_length  = copy_length;
    for (uint32_t i = 0u; i < copy_length; i++) {
        s_current_response.data[i] = response_frame->data[i];
    }
    s_current_state = DIAG_SLOT_SUCCESS;

    s_consecutive_failures = 0u; /* communication recovered - re-arm the Reset Request latch for any future episode */
    s_reset_request_sent   = false;
}

/* TEMPORARY placeholder framing - see this file's top comment. Only maps
 * the already-known Request fields onto 8 raw bytes so Send() has
 * something real to transmit; not a UDS/Protocol implementation. */
static HWProxy_CanFrame build_request_frame(const DiagnosticRequest* request) {
    HWProxy_CanFrame frame;
    frame.id      = request->target;
    frame.dlc     = 8u;
    frame.data[0] = (uint8_t)request->operation;
    frame.data[1] = (uint8_t)(request->parameter >> 24);
    frame.data[2] = (uint8_t)(request->parameter >> 16);
    frame.data[3] = (uint8_t)(request->parameter >> 8);
    frame.data[4] = (uint8_t)(request->parameter);
    frame.data[5] = 0u;
    frame.data[6] = 0u;
    frame.data[7] = 0u;
    return frame;
}

/* One Non-Blocking step of the Current Operation's CAN exchange (section
 * 5: "Request -> Accept -> RUNNING -> Diagnostic_Process() -> Progress ->
 * ... -> SUCCESS/FAILED"). Never waits - either advances by exactly one
 * Send or one Receive attempt, or checks the Timeout, then returns. */
static void advance_current(void) {
    if (!s_current_request_sent) {
        HWProxy_CanFrame request_frame = build_request_frame(&s_current_request);
        HWProxy_CanResult send_result  = HWProxy_CAN_Send(&request_frame);

        if (send_result == HWPROXY_CAN_OK) {
            s_current_request_sent = true;
        } else if (send_result == HWPROXY_CAN_ERROR) {
            fail_current(DIAG_STATUS_BUS_ERROR);
        }
        /* HWPROXY_CAN_BUSY: try sending again on the next Process() call. */
        return;
    }

    HWProxy_CanFrame response_frame;
    if (HWProxy_CAN_Receive(&response_frame) == HWPROXY_CAN_OK) {
        succeed_current(&response_frame);
        return;
    }

    if (Time_Elapsed(s_current_started_tick, DIAG_RESPONSE_TIMEOUT_TICKS)) {
        fail_current(DIAG_STATUS_TIMEOUT);
    }
    /* else: no response yet, still within Timeout - try again next call. */
}

/* Section 4/0: internal periodic Read Parameter, only while genuinely
 * idle (never preempts a real Current/Pending Request) and only in
 * NORMAL Power Mode (see this file's top comment for the LOW_POWER
 * decision). */
static void maybe_start_periodic_read(void) {
    if (Power_GetMode() != POWER_NORMAL) {
        return;
    }
    if (!Time_Elapsed(s_last_periodic_tick, DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS)) {
        return;
    }

    DiagnosticRequest periodic_request;
    periodic_request.operation = DIAG_OP_READ_PARAMETER;
    periodic_request.target    = 0u; /* TODO(open-item): target ECU id for the periodic read - Protocol-specific, owned by the Diagnostic team, see diagnostic_management_config.h */
    periodic_request.parameter = DIAG_PERIODIC_READ_PARAMETER_ID;

    start_operation(&periodic_request, true);
    s_last_periodic_tick = Time_GetTick();
}

void Diagnostic_Init(void) {
    s_current_state         = DIAG_SLOT_IDLE;
    s_current_request_sent  = false;
    s_current_is_periodic   = false;
    s_pending_present       = false;
    s_last_periodic_tick    = Time_GetTick();

    s_consecutive_failures = 0u;
    s_reset_request_sent   = false;

    (void)HWProxy_CAN_Init();
}

void Diagnostic_Process(void) {
    if (s_current_state == DIAG_SLOT_RUNNING) {
        advance_current();
        return; /* one step per call - Non-Blocking discipline (section 5) */
    }

    if ((s_current_state == DIAG_SLOT_SUCCESS || s_current_state == DIAG_SLOT_FAILED) && s_current_is_periodic) {
        /* Bug fix (see s_current_is_periodic's own comment above): nothing
         * external ever calls Diagnostic_GetResponse() for a periodic-
         * origin result (Diagnostic_GetResponse() itself now refuses to
         * hand one out), so this Management must free its own slot itself -
         * otherwise Current would stay "occupied" forever after the very
         * first periodic read, permanently starving both future periodic
         * reads and any external Pending Request. One step per call, same
         * discipline as advance_current() above - just frees Current and
         * returns; a queued Pending Request (if any) is promoted on the
         * NEXT call, exactly like the Diagnostic_GetResponse() consume path
         * below already behaves. s_current_is_periodic is reset here too
         * (not strictly load-bearing today, since IDLE alone already makes
         * Diagnostic_GetResponse()'s state check fail on its own - but
         * relying on that ordering is fragile, so this Current is left in
         * a fully clean/default state instead of a partially-stale one). */
        s_current_state       = DIAG_SLOT_IDLE;
        s_current_is_periodic = false;
        return;
    }

    if (s_current_state == DIAG_SLOT_IDLE) {
        if (s_pending_present) {
            /* Only promoted after a real Consume (Diagnostic_GetResponse()
             * already brought Current back to IDLE) - section 11, never
             * right after SUCCESS/FAILED on its own. */
            DiagnosticRequest promoted = s_pending_request;
            s_pending_present = false;
            start_operation(&promoted, false);
            return;
        }

        maybe_start_periodic_read();
    }
    /* DIAG_SLOT_SUCCESS/DIAG_SLOT_FAILED (non-periodic): nothing to do
     * until the caller consumes it via Diagnostic_GetResponse() - Process()
     * must not touch Current on its own (section 9). */
}

DiagnosticSubmitResult Diagnostic_SubmitRequest(const DiagnosticRequest* request) {
    if (request == NULL) {
        return DIAG_SUBMIT_BUSY; /* defensive - closest "rejected" outcome this Contract's return type has for a caller bug */
    }

    if (s_current_state == DIAG_SLOT_IDLE) {
        start_operation(request, false);
        return DIAG_SUBMIT_ACCEPTED;
    }

    /* Current is occupied - RUNNING, or an unconsumed SUCCESS/FAILED
     * (section 8's key note: unconsumed Result still counts as occupied). */
    if (!s_pending_present) {
        s_pending_request  = *request;
        s_pending_present  = true;
        return DIAG_SUBMIT_PENDING;
    }

    return DIAG_SUBMIT_BUSY;
}

bool Diagnostic_GetResponse(DiagnosticResponse* response) {
    if (response == NULL) {
        return false;
    }
    if (s_current_is_periodic) {
        /* A periodic-origin result is never handed out here, regardless of
         * state - see s_current_is_periodic's own comment above for the bug
         * this prevents (an external caller receiving a stale, unrelated
         * result attributed to its own Request). Diagnostic_Process()'s own
         * periodic-consume branch is the only thing that ever frees this
         * particular Current. */
        return false;
    }
    if (s_current_state != DIAG_SLOT_SUCCESS && s_current_state != DIAG_SLOT_FAILED) {
        return false;
    }

    *response        = s_current_response;
    s_current_state  = DIAG_SLOT_IDLE;
    return true;
}
