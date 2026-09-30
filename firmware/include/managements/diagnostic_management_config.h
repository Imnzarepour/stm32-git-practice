#ifndef MGMT_DIAGNOSTIC_CONFIG_H
#define MGMT_DIAGNOSTIC_CONFIG_H

/* All tunable constants diagnostic_management.c needs, collected in one
 * place - same convention as
 * managements/hardware_input_management_config.h. */

/* "Read Parameter دوره‌ای (حدود ۲ بار در دقیقه)" - diagnostic-management.md
 * section 4/0. 2/minute = every 30s; ticks are 1ms each (time-base.md). */
#define DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS (30000u)

/* TODO(open-item): which Parameter ID is actually read periodically (RPM?
 * Speed? both?) is Protocol/ECU-specific knowledge that belongs to the
 * Diagnostic team, not this architectural Contract (CLAUDE.md: "Diagnostic
 * Engine داخلی مسئولیت تیم Diagnostic جداست"). 0 is a placeholder so the
 * periodic-scheduling mechanism itself is real and testable today; the
 * Diagnostic team must replace this (likely with more than one ID) once
 * the real Parameter list is defined. */
#define DIAG_PERIODIC_READ_PARAMETER_ID (0u)

/* How long Diagnostic_Process() waits for a CAN response to the Current
 * Operation before treating it as DIAG_STATUS_TIMEOUT - an implementation
 * parameter (diagnostic-management.md section 23 explicitly leaves Retry/
 * Timeout counts to the Diagnostic team, not the architecture Contract). */
#define DIAG_RESPONSE_TIMEOUT_TICKS (1000u)

/* --- Module/System Reset escalation (section 22: "در صورت نیاز به Recovery
 * سطح Module/System: Diagnostic -> Reset Request -> System Management ->
 * Reset") ---
 * How many consecutive Operation failures (BUS_ERROR or TIMEOUT, regardless
 * of periodic vs explicit origin - see fail_current()) before this
 * Management gives up locally and submits a Reset Request, instead of
 * silently reporting FAILED forever. Same "count consecutive failures,
 * escalate past a threshold" shape as SERVER_MAX_SEND_FAILURES_BEFORE_RESET
 * (server_management_config.h) - a placeholder pending real product
 * tuning, not a Contract-fixed number. */
#define DIAG_MAX_CONSECUTIVE_FAILURES (5u)

#endif /* MGMT_DIAGNOSTIC_CONFIG_H */
