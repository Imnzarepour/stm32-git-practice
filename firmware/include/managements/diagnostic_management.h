#ifndef MGMT_DIAGNOSTIC_H
#define MGMT_DIAGNOSTIC_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/02-management-contracts/diagnostic-management.md
 * (section 0 of that document: CAN is exclusive to Diagnostic Management -
 * no Arbitration with any other Management, a closed decision).
 *
 * Scope: Read Parameter (explicit request + ~2/minute internal periodic
 * read), Read DTC, Clear DTC, Actuator Test (explicit user decision,
 * OPEN-ITEMS.md - the generic UDS-style "command an actuator to a state
 * and read back the result" service; today's concrete consumer is Door
 * Lock/Unlock via shared/command_router.c's COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST,
 * but this Management itself does not know "door" - only "target ECU +
 * parameter", same boundary as every other operation below). The real
 * CAN/UDS/K-Line Protocol, ECU-specific framing and internal FSM are the
 * separate Diagnostic team's responsibility (CLAUDE.md: "Diagnostic
 * Engine داخلی مسئولیت تیم Diagnostic جداست") - this Contract only fixes
 * the observable boundary Main Firmware sees.
 *
 * Section 25 ("Diagnostic -> Event/Fault -> Event Policy"): every Operation
 * that ends FAILED also produces a local Fault (System Management's shared
 * Fault Queue) and an EVENT_DIAGNOSTIC_FAILURE (shared/event.h, routed by
 * shared/event_policy.c - today: forwarded to Server and persisted to
 * STORAGE_LOG via Log Infrastructure) - see diagnostic_management.c's
 * fail_current()/report_operation_failure(). This Management only ever
 * produces the signal; it never runs Event Policy itself. */

typedef enum {
    DIAG_OP_READ_PARAMETER,
    DIAG_OP_READ_DTC,
    DIAG_OP_CLEAR_DTC,
    DIAG_OP_ACTUATOR_TEST
} DiagnosticOperation;

typedef struct {
    DiagnosticOperation operation;
    uint32_t            target;     /* target ECU id */
    uint32_t            parameter;  /* meaningful for DIAG_OP_READ_PARAMETER (which parameter to read) and DIAG_OP_ACTUATOR_TEST (commanded state - e.g. 1=open/unlock, 0=close/lock for a door actuator; the exact encoding is the Diagnostic team's protocol, not fixed here) */
} DiagnosticRequest;

typedef enum {
    DIAG_STATUS_SUCCESS,
    DIAG_STATUS_TIMEOUT,
    DIAG_STATUS_PROTOCOL_ERROR,
    DIAG_STATUS_NEGATIVE_RESPONSE,
    DIAG_STATUS_INVALID_RESPONSE,
    DIAG_STATUS_BUS_ERROR
} DiagnosticStatus;

typedef struct {
    DiagnosticStatus status;
    uint8_t          data[64]; /* TODO: real size once Diagnostic team fixes the Protocol payload shape */
    uint32_t         data_length;
} DiagnosticResponse;

/* Contract section 8/12: Diagnostic has exactly two logical slots - a
 * Current Operation and (at most) one Pending Request, never a general
 * Queue. A SUCCESS/FAILED Current that has not been consumed via
 * Diagnostic_GetResponse() yet still counts as "occupied" (section 8's
 * own key note). */
typedef enum {
    DIAG_SUBMIT_ACCEPTED, /* became the Current Operation - now RUNNING */
    DIAG_SUBMIT_PENDING,  /* Current was occupied; queued into the single Pending slot */
    DIAG_SUBMIT_BUSY      /* both Current and Pending were already occupied (or request was NULL) - rejected, nothing changed */
} DiagnosticSubmitResult;

void Diagnostic_Init(void);
void Diagnostic_Process(void); /* Non-Blocking - one step per call, see diagnostic_management.c */

DiagnosticSubmitResult Diagnostic_SubmitRequest(const DiagnosticRequest* request);

/* Non-Blocking poll. true only when Current is SUCCESS/FAILED AND it was
 * started by an explicit Diagnostic_SubmitRequest() (directly, or promoted
 * out of Pending) - copies it out and frees Current back to IDLE (allowing
 * a queued Pending Request, if any, to start on the next
 * Diagnostic_Process() call - section 11). Every caller that submits a
 * Request must poll this regularly (section 10) - otherwise Current never
 * frees and a Pending Request starves.
 *
 * A Current started by this Management's own internal periodic Read
 * Parameter (section 4) is NEVER returned here, even while SUCCESS/FAILED -
 * this Management consumes/frees that one itself (see
 * diagnostic_management.c). Bug fix: an external caller used to be able to
 * receive that periodic result by mistake, attributed to its own,
 * completely unrelated Request, whenever its own Request happened to be
 * promoted from Pending right after the periodic one completed - since
 * nothing outside this Management is meant to know the periodic scheduler
 * exists at all (section 4: "Main Firmware فقط نتیجه ... را مصرف می‌کند"
 * refers to that internal cycle's own consumption, not to a caller of this
 * function). */
bool Diagnostic_GetResponse(DiagnosticResponse* response);

#endif
