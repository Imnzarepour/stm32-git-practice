#include "managements/firmware_update_management.h"
#include "managements/firmware_update_management_config.h"
#include "managements/power_management.h"
#include "infrastructure/ec200u_manager.h"
#include "infrastructure/file_transfer.h"
#include "infrastructure/file_transfer_config.h" /* FILE_TRANSFER_MAX_CHUNK_BYTES - reused as-is for the HTTP pull buffer below, no separate constant invented */
#include "infrastructure/storage.h"
#include "infrastructure/time_base.h"
#include "shared/reset_request.h"
#include "shared/event.h"
#include "shared/queues.h"
#include <string.h>

/* Contract: docs/02-management-contracts/firmware-update-management.md
 * See the header for the exact scope (explicit user decision, two parts):
 * Download + Store via File Transfer Infrastructure (which is entirely
 * generic over StorageObject - this file is the only place that says "the
 * destination is STORAGE_FIRMWARE_UPDATE") parks a completed transfer in
 * FIRMWARE_UPDATE_READY; FirmwareUpdate_SubmitInstallRequest() then moves it
 * through VALIDATING/INSTALLING (Boot Action + Reset Request) below.
 *
 * Power-Mode-aware pacing (explicit user decision, OPEN-ITEMS.md: "همه
 * تجهیزات باید در حالت کم‌مصرف نرخ دیگری داشته باشند - با هدف کاهش تعداد
 * پراسس و کم کردن مصرف"): while REQUESTED/DOWNLOADING, this file used to
 * call FileTransfer_StartDownload()/GetStatus() on every single Super
 * Loop tick, unlike every other Management with a background/periodic
 * component (GPS/SMS/Server/Call all widen their own interval in
 * POWER_LOW_POWER - see firmware_update_management_config.h's comment on
 * each constant for the exact precedent). This only throttles how often
 * this Management checks status - it does NOT slow down an in-progress
 * transfer's actual data rate, since chunks are written by Server
 * Management's own FileTransfer_SupplyChunk() calls (server_management.c),
 * independent of this file's own poll cadence. Actuator Management was
 * deliberately left untouched: it is already pure Queue-driven work (zero
 * cost while idle) with no periodic component to throttle, and safety-
 * relevant commands (Engine Cut-off/Alarm) must never be delayed by Power
 * Mode - see actuator_management.c. */

/* Mirrors bootloader/include/boot_control.h's BootControl byte-for-byte
 * (two uint32_t fields at STORAGE_RECOVERY offset 0) - deliberately NOT
 * shared via a common header, since bootloader/ and firmware/ are
 * independent projects that never include each other's headers (CLAUDE.md).
 * If that layout ever changes, this struct must change with it - same
 * discipline already used for BootErrorRecord in system_management.c.
 * active_bank is read back and preserved verbatim (Bootloader is its only
 * owner, see boot_control.h's own comment); this Management only ever
 * overwrites `action`. */
typedef struct {
    uint32_t active_bank;
    uint32_t action;
} FirmwareUpdateBootControl;

#define FIRMWARE_UPDATE_BOOT_CONTROL_OFFSET 0u /* bootloader/include/boot_control.h: STORAGE_RECOVERY_BOOT_CONTROL_OFFSET */
#define FIRMWARE_UPDATE_BOOT_ACTION_PROGRAM 1u /* bootloader/include/boot_action.h: BOOT_ACTION_PROGRAM */

/* Sub-record inside STORAGE_RECOVERY entirely owned by THIS file - unlike
 * FirmwareUpdateBootControl (offset 0, Bootloader-owned) and
 * system_management.c's BootErrorRecord (offset 8, Bootloader writes/
 * Application reads), Bootloader never reads or writes this one; it exists
 * purely so Firmware Update Management can talk to its own next boot. No
 * bootloader/ coordination needed for this offset (unlike the two above) -
 * kept local to firmware/, one more reason not to entangle it with the
 * still-open bootloader/firmware storage-layout questions (OPEN-ITEMS.md #2).
 *
 * Purpose (explicit user decision - EVENT_FIRMWARE_UPDATE_COMPLETED "is an
 * Event like any other - Shock or ACC - whenever it becomes true it must
 * reach the server"): the moment BEFORE this Management requests the
 * install Reset is the only moment Application-side code still knows which
 * Bank was active. On the next boot, comparing the Active Bank then vs now
 * is the only reliable, already-available signal that Bootloader actually
 * performed a real, CRC-verified Bank switch (bootloader.md section 11) -
 * i.e. that the install genuinely completed, as opposed to Bootloader
 * falling back to the previous Bank after a failed Program/CRC (section 9).
 * No bootloader/ changes needed - the comparison happens entirely on the
 * Application side, using the same BootControl.active_bank field
 * FirmwareUpdateBootControl above already reads. */
#define FIRMWARE_UPDATE_PENDING_INSTALL_OFFSET 16u /* after BootControl (0-7) and system_management.c's BootErrorRecord (8-15) */

typedef struct {
    uint32_t pending;              /* 0 = none; 1 = an install Reset was just requested, check on the next boot */
    uint32_t previous_active_bank; /* BootControl.active_bank observed right before that Reset was requested */
} FirmwareUpdatePendingInstallRecord;

static FirmwareUpdateState s_state;
static uint32_t             s_expected_size;
static char                 s_url[FIRMWARE_DOWNLOAD_URL_MAX_LENGTH];

static bool     s_check_started;
static TimeTick s_last_check_tick;

/* Runs once at every boot (called from FirmwareUpdate_Init() below) - same
 * "check a persisted flag at Startup" shape as system_management.c's own
 * check_boot_error(), just for a completed install instead of a failed
 * one. See FirmwareUpdatePendingInstallRecord's own comment for the full
 * reasoning. Non-Blocking (one Storage Read/Read/Write, no loop). */
static void check_install_completed(void) {
    FirmwareUpdatePendingInstallRecord pending;
    FirmwareUpdateBootControl          control;

    if (Storage_Read(STORAGE_RECOVERY, FIRMWARE_UPDATE_PENDING_INSTALL_OFFSET, &pending, sizeof(pending)) != STORAGE_OK) {
        return; /* no real STORAGE_RECOVERY backend yet - same safe no-op as check_boot_error() */
    }
    if (pending.pending == 0u) {
        return; /* no install Reset was pending - an ordinary boot */
    }

    if (Storage_Read(STORAGE_RECOVERY, FIRMWARE_UPDATE_BOOT_CONTROL_OFFSET, &control, sizeof(control)) == STORAGE_OK
        && control.active_bank != pending.previous_active_bank) {
        /* The Active Bank actually changed since we requested the Reset -
         * the only way that happens is Bootloader's own successful
         * Program+CRC+Switch (bootloader.md section 11): a real, confirmed
         * completed install. Same Event/Queue-Push shape every other
         * Detection->Event producer in this firmware already uses (e.g.
         * hardware_input_management.c's Shock/ACC) - Event Policy decides
         * routing from here, this Management does not. */
        Event event;
        event.event_type = EVENT_FIRMWARE_UPDATE_COMPLETED;
        event.event_id    = 0u; /* not used anywhere yet - same placeholder as every other Event producer in this firmware */
        event.timestamp   = Time_GetTick();
        (void)Queue_Push(Queues_GetEventQueue(), &event, sizeof(event));
    }
    /* else: Bank unchanged - Bootloader fell back to the previous Firmware
     * (CRC failure / retries exhausted, bootloader.md section 9), or
     * STORAGE_RECOVERY's BootControl read failed just now - either way, no
     * install actually completed, nothing to report. */

    pending.pending = 0u; /* consumed - never re-check on a later boot */
    (void)Storage_Write(STORAGE_RECOVERY, FIRMWARE_UPDATE_PENDING_INSTALL_OFFSET, &pending, sizeof(pending));
}

void FirmwareUpdate_Init(void) {
    s_state         = FIRMWARE_UPDATE_IDLE;
    s_expected_size = 0;
    s_url[0]        = '\0';

    s_check_started   = false;
    s_last_check_tick = 0;

    check_install_completed();
}

bool FirmwareUpdate_SubmitRequest(uint32_t expected_size, const char* url) {
    if (s_state != FIRMWARE_UPDATE_IDLE && s_state != FIRMWARE_UPDATE_READY && s_state != FIRMWARE_UPDATE_FAILED) {
        return false; /* a Download is already in progress */
    }
    if (expected_size == 0u || url == NULL || url[0] == '\0') {
        return false;
    }

    s_expected_size = expected_size;
    memset(s_url, 0, sizeof(s_url));
    strncpy(s_url, url, sizeof(s_url) - 1u);
    s_state         = FIRMWARE_UPDATE_REQUESTED;
    s_check_started = false; /* force the very first check below to run immediately, not wait out a stale interval from a previous Download */
    return true;
}

bool FirmwareUpdate_SubmitInstallRequest(void) {
    if (s_state != FIRMWARE_UPDATE_READY) {
        return false; /* nothing complete/validated to install right now */
    }
    s_state = FIRMWARE_UPDATE_VALIDATING;
    return true;
}

/* One-shot, unthrottled (unlike REQUESTED/DOWNLOADING below - there is no
 * periodic polling here, just a single Storage Read + Write + Reset
 * Request). See firmware_update_management.h's "SCOPE, PART 2" comment for
 * the full picture and its current limits. */
static void process_validating(void) {
    FirmwareUpdateBootControl control;

    if (Storage_Read(STORAGE_RECOVERY, FIRMWARE_UPDATE_BOOT_CONTROL_OFFSET, &control, sizeof(control)) != STORAGE_OK) {
        /* STORAGE_RECOVERY has no real backend yet (infrastructure/storage.c) -
         * cannot safely guess active_bank, so this cannot proceed. Same
         * "logic real, hardware pending" shape STORAGE_FIRMWARE_UPDATE
         * itself already has throughout this file. */
        s_state = FIRMWARE_UPDATE_FAILED;
        return;
    }

    {
        uint32_t active_bank_before_install = control.active_bank; /* captured before action is overwritten below - see FirmwareUpdatePendingInstallRecord's comment */

        control.action = FIRMWARE_UPDATE_BOOT_ACTION_PROGRAM; /* active_bank left untouched - Bootloader owns that field */
        if (Storage_Write(STORAGE_RECOVERY, FIRMWARE_UPDATE_BOOT_CONTROL_OFFSET, &control, sizeof(control)) != STORAGE_OK) {
            s_state = FIRMWARE_UPDATE_FAILED;
            return;
        }

        /* Best-effort: unlike the BootControl write above, a failure here
         * does not block the install itself - it only means
         * check_install_completed() will not be able to confirm success on
         * the next boot (no EVENT_FIRMWARE_UPDATE_COMPLETED), which is a
         * lesser concern than aborting a real install already in progress. */
        FirmwareUpdatePendingInstallRecord pending;
        pending.pending              = 1u;
        pending.previous_active_bank = active_bank_before_install;
        (void)Storage_Write(STORAGE_RECOVERY, FIRMWARE_UPDATE_PENDING_INSTALL_OFFSET, &pending, sizeof(pending));
    }

    s_state = FIRMWARE_UPDATE_INSTALLING;

    ResetRequest reset_request;
    reset_request.type      = RESET_TYPE_SYSTEM_RESET;
    reset_request.target    = "MCU";
    reset_request.source    = RESET_SOURCE_FIRMWARE_UPDATE;
    reset_request.reason    = RESET_REASON_FIRMWARE_INSTALL_REQUESTED;
    reset_request.timestamp = Time_GetTick();
    System_SubmitResetRequest(&reset_request); /* Non-Blocking, no Ack - reset-request-contract.md §8/§9 */
}

/* Pulls whatever HTTP response bytes EC200U Manager already has buffered
 * and feeds them straight into File Transfer Infrastructure - runs on
 * EVERY FirmwareUpdate_Process() call while DOWNLOADING, deliberately NOT
 * gated by the FIRMWARE_UPDATE_CHECK_INTERVAL_MS throttle below (same
 * property the old MQTT-chunk-forwarding path had: server_management.c
 * used to call FileTransfer_SupplyChunk() every time a chunk arrived,
 * independent of this Management's own poll cadence - see this file's own
 * top comment). Only the STATUS checks (FileTransfer_StartDownload()/
 * GetStatus() below) stay throttled; moving already-available bytes never
 * should be, or Power-Mode pacing would silently cap real download
 * throughput. */
static void pull_http_chunks(void) {
    uint8_t  buffer[FILE_TRANSFER_MAX_CHUNK_BYTES];
    uint32_t received_length;

    if (EC200U_HTTP_GetState() == EC200U_HTTP_ERROR) {
        s_state = FIRMWARE_UPDATE_FAILED;
        return;
    }

    if (EC200U_HTTP_GetChunk(buffer, sizeof(buffer), &received_length) != EC200U_OK) {
        return; /* nothing buffered yet (still IN_PROGRESS), or already fully drained (COMPLETE) - either way, try again next call */
    }
    if (received_length == 0u) {
        return; /* defensive - EC200U_HTTP_GetChunk() should never report EC200U_OK with 0 bytes, but never forward an empty chunk either way */
    }

    if (FileTransfer_SupplyChunk(buffer, received_length) != FILE_TRANSFER_OK) {
        s_state = FIRMWARE_UPDATE_FAILED;
    }
}

void FirmwareUpdate_Process(void) {
    if (s_state == FIRMWARE_UPDATE_VALIDATING) {
        process_validating();
        return;
    }

    if (s_state == FIRMWARE_UPDATE_DOWNLOADING) {
        pull_http_chunks();
        if (s_state != FIRMWARE_UPDATE_DOWNLOADING) {
            return; /* pull_http_chunks() just moved this to FAILED - nothing left to throttle-check below */
        }
    }

    if (s_state != FIRMWARE_UPDATE_REQUESTED && s_state != FIRMWARE_UPDATE_DOWNLOADING) {
        return; /* IDLE/READY/INSTALLING/FAILED - already zero work, nothing to throttle */
    }

    {
        uint32_t interval = FIRMWARE_UPDATE_CHECK_INTERVAL_MS;
        if (Power_GetMode() == POWER_LOW_POWER) {
            interval *= FIRMWARE_UPDATE_LOW_POWER_RATE_MULTIPLIER;
        }
        if (s_check_started && !Time_Elapsed(s_last_check_tick, interval)) {
            return;
        }
        s_check_started   = true;
        s_last_check_tick = Time_GetTick();
    }

    if (s_state == FIRMWARE_UPDATE_REQUESTED) {
        FileTransferResult result = FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, s_expected_size);
        if (result != FILE_TRANSFER_OK) {
            if (result != FILE_TRANSFER_BUSY) {
                /* FILE_TRANSFER_INVALID/FILE_TRANSFER_STORAGE_FAILURE -
                 * nothing this Management can locally recover from here
                 * (e.g. STORAGE_FIRMWARE_UPDATE has no real NOR backend
                 * yet - see infrastructure/storage.c). FILE_TRANSFER_BUSY
                 * alone is retried on a later check instead of failing
                 * outright, since it only means "File Transfer is still
                 * finishing a previous, unrelated transfer". */
                s_state = FIRMWARE_UPDATE_FAILED;
            }
            return;
        }

        if (EC200U_HTTP_StartGet(s_url) != EC200U_OK) {
            s_state = FIRMWARE_UPDATE_FAILED; /* Storage side already started - Cancel() is not needed, a later StartDownload() for the next attempt simply starts a fresh transfer */
            return;
        }
        s_state = FIRMWARE_UPDATE_DOWNLOADING;
    } else { /* FIRMWARE_UPDATE_DOWNLOADING - the only other state that reaches here */
        FileTransferStatus status = FileTransfer_GetStatus();
        if (status == FILE_TRANSFER_COMPLETED) {
            s_state = FIRMWARE_UPDATE_READY; /* staged in NOR - "فقط آماده‌سازی می‌کند", nothing beyond this in this pass */
        } else if (status == FILE_TRANSFER_FAILED || status == FILE_TRANSFER_CANCELLED) {
            s_state = FIRMWARE_UPDATE_FAILED;
        }
        /* PENDING/TRANSFERRING/IDLE (not yet caught up) - keep waiting, pull_http_chunks() above is what actually moves bytes now */
    }
}

FirmwareUpdateState FirmwareUpdate_GetState(void) {
    return s_state;
}
