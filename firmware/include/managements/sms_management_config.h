#ifndef MGMT_SMS_CONFIG_H
#define MGMT_SMS_CONFIG_H

/* Every tunable constant for SMS Management lives in this one file, same
 * convention as managements/hardware_input_management_config.h (gain/
 * debounce/threshold there). None of these are part of the Contract
 * (docs/02-management-contracts/sms-management.md) - the Contract
 * explicitly leaves Retry count/timing to "this Management's own
 * policy" (section 12/14). Changing a value here never requires
 * touching sms_management.c/.h logic, only this file.
 *
 * Unlike hardware_input_management_config.h, this file is included by
 * BOTH sms_management.h (SMS_MAX_DESTINATION_LENGTH/SMS_MAX_PAYLOAD_LENGTH
 * size the public SmsRequest struct, so they must be visible wherever
 * that struct is used) and sms_management.c (everything else, internal
 * policy only). */

/* --- SmsRequest sizing (contract 9: SmsRequest{Destination,Payload}) --- */
#define SMS_MAX_DESTINATION_LENGTH 20u  /* E.164 number: up to 15 digits + '+' + margin */
#define SMS_MAX_PAYLOAD_LENGTH     161u /* Single GSM-7 SMS: 160 chars + NUL - no concatenated/multipart SMS yet */

/* --- Send retry policy (contract 12: "Retry ارسال SMS در مالکیت SMS Management است") ---
 * One initial attempt + this many retries, each gated by the delay below.
 * Placeholders pending real product tuning. */
#define SMS_SEND_MAX_RETRIES    3u
#define SMS_SEND_RETRY_DELAY_MS 5000u

/* --- Power Mode (contract 14: "SMS و Power Mode") ---
 * "Reduced SMS Activity" in LOW_POWER, same idea/shape as
 * SERVER_LOW_POWER_MIN_SEND_INTERVAL_MS in server_management_config.h: a
 * minimum spacing enforced between successive outgoing send attempts
 * (first attempt of a new item, or a retry of the current one). This is
 * a separate, additional gate on top of SMS_SEND_RETRY_DELAY_MS above,
 * not a multiplier on it - the retry backoff is a fixed reaction to a
 * failure (same in both Power Modes, contract 12), while this is a
 * Power-Mode-driven pacing applied uniformly to every attempt so a
 * chain of quick successes in LOW_POWER is also spaced out, not just
 * failures. Contract 14 is explicit that SMS Reception must NOT be
 * slowed by Power Mode at all, so this constant is only read from
 * advance_send() (outgoing), never from process_incoming(). */
#define SMS_LOW_POWER_MIN_SEND_INTERVAL_MS 5000u

/* --- Fault reporting ---
 * TODO(open-item): the system-wide Fault struct is still open
 * (OPEN-ITEMS.md red #5). This code is local/provisional, same pattern
 * as EVENT_POLICY_FAULT_CODE_UNDEFINED (shared/event_policy.h). */
#define SMS_FAULT_CODE_SEND_FAILED 2001u

/* --- Event Policy alert composition (SMS_QUEUE_ITEM_ALERT path) ---
 * The outgoing alert destination itself is no longer a compile-time
 * constant here - it lives in Configuration Infrastructure's EEPROM-backed
 * DeviceConfig.access.sms_alert_destination (infrastructure/configuration.h),
 * read directly by sms_management.c. The real number is still an open
 * product question (same one this constant used to flag); only its
 * storage location changed, same migration already done for
 * allowed_phone_numbers. Per-event wording is unaffected and stays local
 * to sms_management.c's alert_text_for_event(). */

/* --- Position Request command (SMS -> GPS Position reply, no Actuation) ---
 * TODO(ask user): real SMS command syntax is still an open product
 * question - see the long-standing "OPEN QUESTION" comment at the top of
 * sms_management.h (sms_try_parse_command() flags the same gap for
 * Actuator-bound commands). This keyword is a working PLACEHOLDER so the
 * data path is real/testable today (explicit user decision - see
 * OPEN-ITEMS.md), NOT a final syntax. Matched case-sensitively against the
 * full, trimmed-nowhere incoming SMS payload - exact match only, no
 * prefix/suffix/whitespace tolerance yet (sms_management.c's
 * sms_try_handle_position_request()). */
#define SMS_POSITION_REQUEST_KEYWORD "LOC"

/* --- Position reply text ---
 * TODO(ask user): same provisional status as the keyword above - real
 * wording/units/precision/localization is an open product question, not
 * decided anywhere in docs/. The numeric reply itself
 * ("POS:<lat>.<frac>,<lon>.<frac>", decimal degrees, 6-decimal precision)
 * is built in sms_management.c's format_position_reply() - only the
 * "no fix yet" marker is a plain constant here. */
#define SMS_POSITION_REPLY_NO_FIX_TEXT "POS:NO_FIX"

/* --- Door Actuator Test command (SMS -> Diagnostic Actuator Test -> Door
 * Lock/Unlock, explicit user decision - OPEN-ITEMS.md 🔴 critical item,
 * Pass-Through by explicit request, NOT because this Management judged it
 * safe) --- TODO(ask user): real SMS command syntax is still an open
 * product question, same status as SMS_POSITION_REQUEST_KEYWORD above -
 * these are working PLACEHOLDER keywords so the data path is real/
 * testable today. Matched the same way (exact, case-sensitive, no
 * trimming) - see sms_management.c's sms_try_handle_door_command(). */
#define SMS_DOOR_OPEN_KEYWORD  "DOOR_OPEN"
#define SMS_DOOR_CLOSE_KEYWORD "DOOR_CLOSE"

/* TODO(ask user): which ECU actually owns the door actuator, and what
 * real target id it needs, is Diagnostic-team/vehicle-wiring knowledge
 * this Management has no way to know - same "do not guess the Diagnostic
 * team's protocol" boundary CLAUDE.md states project-wide. Placeholder so
 * DiagnosticRequest.target (diagnostic_management.h) is a real,
 * testable value today. */
#define SMS_DOOR_ACTUATOR_TARGET_ECU_ID 0x100u

/* DiagnosticRequest.parameter encoding for DIAG_OP_ACTUATOR_TEST when
 * reached via this command - see diagnostic_management.h's comment on
 * that field. Arbitrary but documented choice (1=commanded-open,
 * 0=commanded-closed); the Diagnostic team's real protocol may use a
 * different encoding entirely once it exists. */
#define SMS_DOOR_ACTUATOR_PARAMETER_OPEN  1u
#define SMS_DOOR_ACTUATOR_PARAMETER_CLOSE 0u

/* --- Door GPIO Relay command (SMS -> Actuator Management's own direct
 * GPIO output -> Door Lock/Unlock, explicit user decision - second,
 * independent execution mechanism alongside the Diagnostic Actuator Test
 * pair above, "۲ کامند جدا" per the user's own words - OPEN-ITEMS.md 🔴
 * critical item) --- TODO(ask user): same provisional-syntax status as
 * every other SMS keyword in this file - working PLACEHOLDERs, not a
 * final syntax. Deliberately spelled differently from
 * SMS_DOOR_OPEN_KEYWORD/SMS_DOOR_CLOSE_KEYWORD above so a sender can
 * address either mechanism explicitly, matched the same way (exact,
 * case-sensitive) - see sms_management.c's sms_try_handle_door_gpio_command(). */
#define SMS_DOOR_OPEN_GPIO_KEYWORD  "DOOR_OPEN_GPIO"
#define SMS_DOOR_CLOSE_GPIO_KEYWORD "DOOR_CLOSE_GPIO"

#endif /* MGMT_SMS_CONFIG_H */
