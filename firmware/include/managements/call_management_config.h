#ifndef MGMT_CALL_CONFIG_H
#define MGMT_CALL_CONFIG_H

/* Every tunable constant for Call Management lives in this one file, same
 * convention as sms_management_config.h / gps_management_config.h. None
 * of these are part of the Contract (docs/02-management-contracts/
 * call-management.md) - section 5 explicitly leaves this kind of detail
 * to "Contract پیاده‌سازی" (implementation-level decisions). Every
 * numeric value below is a placeholder pending real product/EC200U
 * timing data; changing one never requires touching call_management.c's
 * logic. */

/* --- CallRequest destination sizing ---
 * Same E.164-based sizing as SMS_MAX_DESTINATION_LENGTH
 * (sms_management_config.h): up to 15 digits + '+' + margin. Used only
 * internally (call_management.c) to copy the caller-owned
 * CallRequest.destination pointer into a Management-owned buffer before
 * it is queued - see CallQueueItem's comment in call_management.c for
 * why a raw pointer cannot be queued as-is. */
#define CALL_MAX_DESTINATION_LENGTH 20u

/* --- Outgoing call setup ("No Connection") timeout, contract section 10 ---
 * How long, from the moment EC200U_Call_Dial() is (re)issued, Call
 * Management waits for the call to reach CONNECTED (EC200U_CALL_ACTIVE)
 * before treating this attempt as failed. Deliberately spans both the
 * CALL_OUTGOING and CALL_RINGING public states for an outgoing call -
 * contract 10's example only distinguishes "No Connection" vs. an
 * actually connected call, not a separate dialing-only vs. ringing-only
 * budget. Applies only to calls this Management itself initiated
 * (CALL_ORIGIN_OUTGOING); an incoming call is allowed to keep ringing
 * indefinitely as far as this Management is concerned - deciding
 * otherwise (e.g. auto-reject after N seconds) would be Policy, not
 * Capability (contract section 12/16). */
#define CALL_NO_CONNECTION_TIMEOUT_MS 30000u

/* One initial attempt + this many retries (same Dial attempt, same
 * destination) before this specific CallRequest is given up on -
 * contract section 10 "Retry / Failure", left to this Management's own
 * policy per section 10/12. */
#define CALL_MAX_DIAL_ATTEMPTS 3u

/* --- Termination completion timeout, contract section 9/11 ("Call
 * Termination Failure") ---
 * How long Call Management waits, after issuing EC200U_Call_Hangup()
 * (either because Call_Terminate() was called, or because it gave up
 * locally on a stuck outgoing attempt), for the modem to actually settle
 * back to IDLE/ENDED, before forcing its own public State back to
 * CALL_IDLE anyway. This ceiling exists purely to satisfy the
 * Non-Blocking rule (state-operation-contract.md section 1):
 * Call_Process() must never wait indefinitely for this confirmation. */
#define CALL_HANGUP_TIMEOUT_MS 10000u

/* --- Fault -> Reset Request escalation, contract section 11 ---
 * Number of full Operation failures in a row (either
 * CALL_MAX_DIAL_ATTEMPTS dial attempts exhausted, or a
 * CALL_HANGUP_TIMEOUT_MS termination timeout) before Call Management
 * gives up on Local Recovery and submits a Reset Request for EC200U to
 * System Management (reset-request-contract.md). The counter is reset
 * back to zero the moment any call genuinely reaches CONNECTED - see
 * enter_connected() in call_management.c. Same pattern/naming convention
 * as GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS in gps_management_config.h. */
#define CALL_MAX_CONSECUTIVE_OPERATION_FAILURES 3u

/* --- Background incoming-call poll cadence, contract section 13 (Power Mode) ---
 * How often Call_Process() reads EC200U_Call_GetState() while completely
 * IDLE (no call in progress, nothing queued to dial) - purely to notice
 * an incoming call. This is the ONLY activity Power Mode is allowed to
 * slow down here (contract 13: "در صورت وجود یک Call Request معتبر، Low
 * Power نباید مانع اجرای Capability تماس شود"): the moment a call is in
 * progress (any public State other than CALL_IDLE), every subsequent
 * Process() cycle polls unconditionally regardless of Power Mode - see
 * poll_and_advance()'s background_only flag in call_management.c. */
#define CALL_IDLE_POLL_INTERVAL_MS 1000u
#define CALL_LOW_POWER_POLL_RATE_MULTIPLIER 4u

/* --- Fault reporting ---
 * TODO(open-item): the system-wide Fault struct is still open
 * (OPEN-ITEMS.md red #5). Local/provisional, same pattern as
 * SMS_FAULT_CODE_SEND_FAILED (sms_management_config.h). */
#define CALL_FAULT_CODE_SETUP_FAILED       3001u
#define CALL_FAULT_CODE_TERMINATION_FAILED 3002u

/* --- Event Policy alert call destination (contract section 12 example:
 * "Shock Event -> Event Policy -> Call Request -> Call Management") ---
 * shared/event_policy.c already dispatches EVENT_SHOCK_DETECTED onto
 * Queues_GetCallRequestQueue() as a CallQueueItem tagged
 * CALL_QUEUE_ITEM_ALERT, carrying only an EventAlertRequest (event_type +
 * timestamp, no destination - Event Policy does not know phone numbers,
 * exactly like the SMS alert path). Call Management supplies its own
 * destination for this kind of Policy-originated request.
 *
 * Bug fix: the outgoing alert destination used to be a compile-time
 * constant here (CALL_ALERT_DESTINATION_PLACEHOLDER) that call_management.c
 * read directly - unlike sms_management.c's analogous alert path, which
 * had already migrated to Configuration Infrastructure. The Configuration
 * field/setter for this exact purpose
 * (DeviceConfig.access.call_alert_destination,
 * Configuration_SetCallAlertDestination(), infrastructure/configuration.h)
 * already existed and was simply never read here - call_management.c's
 * try_start_next() now reads it directly, same migration
 * sms_management_config.h's own former SMS_ALERT_DESTINATION_PLACEHOLDER
 * already went through. The real number is still an open product
 * question (same one this constant used to flag); only its storage
 * location changed. */

#endif /* MGMT_CALL_CONFIG_H */
