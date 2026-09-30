#ifndef SHARED_COMMAND_SAFETY_POLICY_H
#define SHARED_COMMAND_SAFETY_POLICY_H
#include <stdbool.h>

/* OPEN-ITEMS.md red item ("Command Safety Policy for sensitive commands")
 * is still open - nobody has defined "is it safe to execute this command
 * right now" (e.g. is the vehicle moving) for Engine Cut-off / Door
 * Lock-Unlock yet. This file is the explicit, single "door" for that
 * question - every caller that routes an externally-originated Command
 * toward execution (today: shared/command_router.c) must go through
 * CommandSafetyPolicy_IsAllowed() instead of deciding on its own, so that
 * when the real policy is written, editing THIS file is the only change
 * needed anywhere in the firmware.
 *
 * Explicit user decision (see OPEN-ITEMS.md): commands that only read or
 * clear Diagnostic memory (COMMAND_DOMAIN_DIAGNOSTIC - Read Parameter/Read
 * DTC/Clear DTC, docs/02-management-contracts/diagnostic-management.md)
 * do not actuate anything physical on the vehicle, unlike Engine Cut-off/
 * Door Lock-Unlock - so this domain is allowed through unconditionally for
 * now, specifically so a real data path exists end-to-end today. Every
 * other domain (Actuator commands first) is fail-closed (rejected) by
 * default until the user actually writes its policy - same Fail-Closed
 * convention already used for Call_IsNumberAllowed()'s empty allowlist. */

typedef enum {
    /* Diagnostic-only commands (Read Parameter / Read DTC / Clear DTC) -
     * no physical actuation, see the file comment above for why this one
     * domain is unconditionally allowed today. */
    COMMAND_DOMAIN_DIAGNOSTIC,
    /* Reading (never writing/actuating) GPS Management's last known
     * Position in reaction to an authorized inbound SMS request
     * (managements/sms_management.c's sms_try_handle_position_request()).
     * Same "a READ is not Actuation" reasoning as COMMAND_DOMAIN_DIAGNOSTIC
     * above - allowed unconditionally today, explicit user decision. This
     * gate is IN ADDITION TO, not instead of, the sender allowlist check
     * (Call_IsNumberAllowed()) sms_management.c already does first - this
     * Policy is about "is a Position reply allowed to go out at all right
     * now", not "who is allowed to ask". TODO(ask user, tracked in
     * OPEN-ITEMS.md): a real policy for this domain (e.g. rate limiting,
     * time-of-day, minimum interval between replies) has not been
     * defined - unconditional allow is a placeholder to open the data
     * path, not a final decision. */
    COMMAND_DOMAIN_GPS_POSITION_READ,
    /* Starting a Firmware Download INTO STORAGE_FIRMWARE_UPDATE (NOR) -
     * see managements/firmware_update_management.c. Explicit user
     * decision (OPEN-ITEMS.md): allowed unconditionally today because,
     * per this pass's own explicit scope, Download+Store alone never
     * touches the running vehicle - no Validation, no Boot Action, no
     * Reset Request happen anywhere in this path (the user's own words:
     * "ورود به بوت‌لودر جزو خواسته من نیست"). This is NOT the same risk
     * class as Engine Cut-off/Door Lock and must stay that way - a
     * future COMMAND_DOMAIN_FIRMWARE_INSTALL (or equivalent) for the
     * actual Boot Action/Reset step belongs with Actuator-class commands
     * below (fail-closed by default, real policy required), never folded
     * into this domain's already-open gate.
     * TODO(ask user, tracked in OPEN-ITEMS.md): two more gates were
     * explicitly deferred to "the door, for later" rather than decided
     * now - (1) whether the requested file/expected_size should be
     * policy-checked before a Download is even allowed to start, and
     * (2) whether the downloaded bytes are legitimate (source/version/
     * signature) before FIRMWARE_UPDATE_READY means anything. Neither is
     * implemented - this domain is unconditional pass-through, same
     * placeholder status as the two domains above. */
    COMMAND_DOMAIN_FIRMWARE_DOWNLOAD,
    /* Diagnostic Actuator Test (docs/02-management-contracts/
     * diagnostic-management.md's DIAG_OP_ACTUATOR_TEST) reaching a real
     * physical Actuator - today's concrete use: Door Lock/Unlock via
     * managements/sms_management.c and server_management.c's inbound
     * Command path, executed through Diagnostic Management/CAN. This is
     * the "via Diagnostic" mechanism; COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO
     * below is the second, independent "via direct GPIO relay" mechanism
     * for the exact same physical action - both exist side by side by
     * explicit user request ("۲ کامند جدا"), neither replaces the other.
     *
     * UNLIKE every domain above, this ONE PHYSICALLY ACTUATES the
     * vehicle - it is the exact scenario OPEN-ITEMS.md's 🔴 critical
     * Command Safety Policy item warns about (Door Lock/Unlock named
     * explicitly alongside Engine Cut-off). Explicit, deliberate user
     * decision (OPEN-ITEMS.md, tracked there in detail): allowed
     * unconditionally today anyway, specifically so the data path
     * (SMS/Server -> Command Safety Policy -> Command Router -> Diagnostic)
     * is real and testable end-to-end, NOT because this Management judged
     * it safe. TODO(ask user, tracked in OPEN-ITEMS.md): a real policy
     * (e.g. vehicle speed/ACC state check before allowing this) has not
     * been defined - this is Pass-Through by explicit request, not by
     * this file's own judgment. Do not treat this case as precedent for
     * silently opening any other Actuator-class domain (e.g. Engine
     * Cut-off) without the same explicit confirmation. */
    COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST,
    /* Door Lock/Unlock via Actuator Management's own direct GPIO relay
     * output (managements/actuator_management.c's ACTUATOR_DOOR_LOCK/
     * ACTUATOR_DOOR_UNLOCK, now wired to real HWProxy_WriteDigitalLine()
     * calls - see that file's LINE_DOOR_LOCK/LINE_DOOR_UNLOCK). Second,
     * independent execution mechanism for the same physical action as
     * COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST above - explicit user
     * request for two separate commands/domains, not one mechanism
     * replacing the other (a caller picks whichever it means to use).
     * Same exact risk class and same exact Pass-Through-by-explicit-
     * request status as that domain - see its comment above, everything
     * there applies here too (this is NOT this file's own safety
     * judgment, and must not be read as precedent for any other
     * Actuator-class domain, e.g. Engine Cut-off). */
    COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO,
    /* Server -> "enter Boot, install the already-downloaded image"
     * (managements/firmware_update_management.c's FirmwareUpdate_SubmitInstallRequest(),
     * only reachable once a prior COMMAND_DOMAIN_FIRMWARE_DOWNLOAD reached
     * FIRMWARE_UPDATE_READY). Explicit user decision, given by direct
     * analogy to COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST/
     * COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO above ("مثل درب باز کن") plus a
     * concrete description of the intended mechanism (defined NOR
     * partition -> EEPROM flags -> Reset -> Bootloader reads NOR, programs
     * Bank 2, CRC decides Bank 2 vs falls back to Bank 1) - this matches
     * the pipeline already sketched in OPEN-ITEMS.md #2 and
     * file-transfer-infrastructure.md §14.
     *
     * UNLIKE the two Actuator domains above, this is not a single GPIO
     * pulse - accepting it writes Boot Action=PROGRAM into STORAGE_RECOVERY
     * and issues a real SYSTEM_RESET (RESET_REASON_FIRMWARE_INSTALL_REQUESTED),
     * i.e. the device leaves normal operation and re-enters the
     * Bootloader. Pass-Through here is explicit user request, not this
     * file's own safety judgment, same discipline as every Actuating
     * domain above. TODO(ask user, tracked in OPEN-ITEMS.md): still no
     * real package validation (signature/version/compatibility) before
     * this domain is allowed - firmware-update-management.md's own
     * non-negotiable principle ("Firmware نامعتبر نباید برای اجرا در
     * اختیار Bootloader قرار گیرد") is only satisfied today by Bootloader's
     * own post-Program CRC check (bank falls back to Bank 1 on failure,
     * never bricking), not by any check before this Reset is requested. */
    COMMAND_DOMAIN_FIRMWARE_INSTALL
    /* Add a new domain here (e.g. COMMAND_DOMAIN_ACTUATOR for Engine
     * Cut-off) only once a real policy exists for it -
     * CommandSafetyPolicy_IsAllowed()'s default case rejects anything not
     * explicitly listed here, so a new domain is fail-closed by
     * construction until it is both added here AND given a real case in
     * the .c file. */
} CommandDomain;

/* Non-Blocking, no side effects - same access pattern as Power_GetMode().
 * Returns true only if `domain` is currently allowed to execute. */
bool CommandSafetyPolicy_IsAllowed(CommandDomain domain);

#endif
