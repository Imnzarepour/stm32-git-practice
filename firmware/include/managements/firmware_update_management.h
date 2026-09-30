#ifndef MGMT_FIRMWARE_UPDATE_H
#define MGMT_FIRMWARE_UPDATE_H
#include "shared/command_router.h" /* FIRMWARE_DOWNLOAD_URL_MAX_LENGTH - single source of truth shared with the wire decode in server_management.c */
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/02-management-contracts/firmware-update-management.md
 * Independence from Server Communication is closed (see OPEN-ITEMS.md's
 * ✅ decided section) - the stale "!! TODO(open-item) !!" this comment
 * used to carry about that question is gone.
 *
 * SCOPE, PART 1 (explicit user decision, OPEN-ITEMS.md): Download + Store
 * into STORAGE_FIRMWARE_UPDATE - "این Management فقط آماده‌سازی می‌کند"
 * (docs/03-infrastructure-contracts/bootloader.md section 5's own
 * boundary). A completed Download parks in FIRMWARE_UPDATE_READY.
 * UPDATED (binary-protocol-final.md, OPEN-ITEMS.md TCU<->Backend #12): the
 * transfer itself now pulls its bytes from EC200U Manager's HTTP Client
 * Capability (this Management calls EC200U_HTTP_StartGet(url)/GetChunk()
 * itself, in FirmwareUpdate_Process() below) instead of Server Management
 * forwarding MQTT-carried chunks into infrastructure/file_transfer.h's
 * FileTransfer_SupplyChunk() - File Transfer Infrastructure's own role
 * (Storage_Write/Erase/Retry/Timeout onto STORAGE_FIRMWARE_UPDATE) and API
 * are completely unchanged, only WHO calls FileTransfer_SupplyChunk() and
 * WHERE the bytes come from changed.
 *
 * SCOPE, PART 2 (explicit user decision, a later session, given by direct
 * analogy to the Door Lock/Unlock precedent - "قبلا یک مسیر مشابه برای باز
 * کردن درب از سرور ایجاد کرده بودم؛ بر اساس اون برو جلو" - plus a concrete
 * description of the intended mechanism): FirmwareUpdate_SubmitInstallRequest()
 * below now actually moves READY -> FIRMWARE_UPDATE_VALIDATING ->
 * FIRMWARE_UPDATE_INSTALLING, writing Boot Action=PROGRAM into
 * STORAGE_RECOVERY (offset shared with bootloader/include/boot_control.h,
 * hand-synced the same way BootErrorRecord already is in
 * system_management.c) and issuing a real Reset Request
 * (RESET_REASON_FIRMWARE_INSTALL_REQUESTED) so Bootloader takes over on the
 * next boot - docs/03-infrastructure-contracts/bootloader.md section 18,
 * OPEN-ITEMS.md #2's sketched pipeline. FIRMWARE_UPDATE_COMPLETED is still
 * never reached by this Management (there is no code left running to reach
 * it after the Reset). "VALIDATING" here is still only the minimal
 * completeness guarantee FILE_TRANSFER_COMPLETED already implies (every
 * expected byte received) - real package validation (signature/version/
 * compatibility) is still an explicit open item, same as
 * COMMAND_DOMAIN_FIRMWARE_DOWNLOAD's own TODO in
 * shared/command_safety_policy.h; Bootloader's own post-Program CRC check
 * (falls back to the previous Bank on failure, never bricking) is the only
 * real safety net in this path today.
 *
 * SCOPE, PART 3 (explicit user decision: "این یک ایونته... هیچ فرقی با شاک
 * سنسور یا ایونت روشن شدن نداره. هروقت فعال بشه باید به سرور برسه" -
 * EVENT_FIRMWARE_UPDATE_COMPLETED is an ordinary Event like any other
 * Detection->Event producer in this firmware, nothing more): even though
 * this Management's own FSM never lives past the Reset in SCOPE PART 2
 * above, FirmwareUpdate_Init() now checks - on every boot - whether an
 * install Reset was pending and, if the Active Bank actually changed since
 * then (the only reliable signal available that Bootloader performed a
 * real CRC-verified switch, bootloader.md section 11), pushes
 * EVENT_FIRMWARE_UPDATE_COMPLETED to the Event Queue. No Bootloader
 * changes needed - see firmware_update_management.c's
 * FirmwareUpdatePendingInstallRecord/check_install_completed() for the
 * exact mechanism (a second, Application-only sub-record in
 * STORAGE_RECOVERY, never touched by Bootloader). A failed install (Bank
 * unchanged, Bootloader fell back) correctly produces no Event - "whenever
 * it becomes true," not unconditionally on every post-install boot. */

typedef enum {
    FIRMWARE_UPDATE_IDLE,
    FIRMWARE_UPDATE_REQUESTED,
    FIRMWARE_UPDATE_DOWNLOADING,
    FIRMWARE_UPDATE_VALIDATING,  /* not reached by this pass - see file header */
    FIRMWARE_UPDATE_READY,
    FIRMWARE_UPDATE_INSTALLING, /* not reached by this pass - see file header */
    FIRMWARE_UPDATE_FAILED,
    FIRMWARE_UPDATE_COMPLETED   /* not reached by this pass - see file header */
} FirmwareUpdateState;

void FirmwareUpdate_Init(void);
void FirmwareUpdate_Process(void); /* Non-Blocking - one step per call */

/* Starts a new Download of `expected_size` bytes from `url` into
 * STORAGE_FIRMWARE_UPDATE. Per docs/02-management-contracts/
 * binary-protocol-final.md section 5/9-2 (OPEN-ITEMS.md TCU<->Backend
 * #12, explicit user decision): the transfer itself goes over HTTP (EC200U
 * Manager's HTTP Client Capability, infrastructure/ec200u_manager.h), NOT
 * the MQTT Command channel - `url` is what FirmwareUpdate_Process() hands
 * to EC200U_HTTP_StartGet() once REQUESTED is reached. `url` is copied
 * immediately into a fixed FIRMWARE_DOWNLOAD_URL_MAX_LENGTH buffer
 * (truncated if longer, same convention Configuration Infrastructure's
 * string setters use), no ownership kept over the caller's pointer
 * afterward. Returns false (no state change) if a Download is already in
 * progress (state is neither IDLE/READY/FAILED), expected_size is 0, or
 * url is NULL/empty. TODO(ask user, tracked in OPEN-ITEMS.md): which file
 * is allowed to be requested and whether it is legitimate (source/version/
 * signature) are explicit future policy questions - not decided or
 * guessed here; today ANY url/expected_size the caller passes is accepted
 * at this layer. */
bool FirmwareUpdate_SubmitRequest(uint32_t expected_size, const char* url);

/* Starts Installing whatever is currently parked in FIRMWARE_UPDATE_READY.
 * Returns false (no state change) if the state is not READY - there is
 * nothing else to reason about (already installing, still downloading, or
 * nothing downloaded at all). On success, moves to FIRMWARE_UPDATE_VALIDATING
 * and the next FirmwareUpdate_Process() call does the rest (Boot Action
 * write + Reset Request) - see the header comment above ("SCOPE, PART 2")
 * for the exact mechanism and its current limits. */
bool FirmwareUpdate_SubmitInstallRequest(void);

FirmwareUpdateState FirmwareUpdate_GetState(void);

#endif
