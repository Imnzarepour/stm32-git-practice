#include "infrastructure/file_transfer.h"
#include "infrastructure/time_base.h"
#include <string.h>
#include <stddef.h>

/* Contract source: docs/03-infrastructure-contracts/file-transfer-infrastructure.md
 *
 * Scope of this implementation:
 *   - Destination is any StorageObject (infrastructure/storage.h) - this
 *     file never hardcodes STORAGE_FIRMWARE_UPDATE or anything specific to
 *     one use case. The Contract's own principle (section 3/19) is that
 *     File Transfer does not know what the file means; a future caller
 *     could target STORAGE_FIRMWARE_UPDATE (firmware image), a Diagnostic
 *     Dump object, a Log Upload object, etc. with the exact same functions.
 *   - "Server Data" (Contract section 6) is modeled as a caller-supplied
 *     push via FileTransfer_SupplyChunk() - Server Management/EC200U are
 *     not wired into this file. Nothing here decides HOW bytes arrive,
 *     only what happens once they do.
 *   - Upload (Contract section 4, "ارسال فایل به Server") is intentionally
 *     NOT implemented yet - this first pass covers Download only, per the
 *     concrete need today (download a file and write it into NOR through
 *     Storage). Upload is a natural, mostly-symmetric follow-up.
 *
 * The real NOR/Memory module is still with an outside contractor
 * (docs/05-outsourcing/nor-storage-outsourcing-spec.md) and STORAGE_FIRMWARE_UPDATE
 * has no backend yet (infrastructure/storage.c always returns
 * STORAGE_ERROR for it) - so in production, every real transfer fails
 * today with FILE_TRANSFER_STORAGE_FAILURE / FILE_TRANSFER_FAULT_STORAGE.
 * That is a real, traceable result, not a crash or a silent no-op - see
 * file_transfer.h's Traceability note. Unit tests mock infrastructure/
 * storage.h directly (Mockstorage.h, the same seam test_system_management.c
 * already uses) to exercise the success/BUSY/failure paths without
 * waiting for that module. */

static FileTransferStatus s_status;
static FileTransferFault  s_fault;
static StorageObject      s_destination;
static uint32_t           s_expected_size;
static uint32_t           s_transferred_size;
static TimeTick            s_last_activity_tick;

/* A chunk that was handed to Storage_Write() but not yet confirmed
 * (STORAGE_BUSY) - buffered here so FileTransfer_Process() can retry it
 * without the caller having to keep its own pointer alive. */
static bool     s_has_pending_chunk;
static uint8_t  s_pending_chunk[FILE_TRANSFER_MAX_CHUNK_BYTES];
static uint32_t s_pending_chunk_length;
static uint32_t s_pending_chunk_offset;

void FileTransfer_Init(void) {
    s_status              = FILE_TRANSFER_IDLE;
    s_fault               = FILE_TRANSFER_FAULT_NONE;
    s_destination         = STORAGE_CONFIG; /* arbitrary safe placeholder - unused while IDLE, overwritten by the next StartDownload() */
    s_expected_size       = 0u;
    s_transferred_size    = 0u;
    s_last_activity_tick  = 0u;
    s_has_pending_chunk   = false;
    s_pending_chunk_length = 0u;
    s_pending_chunk_offset = 0u;
}

/* Tries to durably write the buffered pending chunk. Called both from
 * FileTransfer_SupplyChunk() (first attempt) and FileTransfer_Process()
 * (retry while STORAGE_BUSY) - this is the only place s_transferred_size
 * advances or s_status leaves PENDING/TRANSFERRING, so success/failure is
 * decided in exactly one spot. */
static FileTransferResult attempt_write_pending_chunk(void) {
    StorageResult result = Storage_Write(s_destination, s_pending_chunk_offset, s_pending_chunk, s_pending_chunk_length);

    if (result == STORAGE_BUSY) {
        return FILE_TRANSFER_OK; /* still pending - next Process() call retries it */
    }
    if (result != STORAGE_OK) {
        s_status            = FILE_TRANSFER_FAILED;
        s_fault             = FILE_TRANSFER_FAULT_STORAGE;
        s_has_pending_chunk = false;
        return FILE_TRANSFER_STORAGE_FAILURE;
    }

    s_transferred_size   += s_pending_chunk_length;
    s_has_pending_chunk   = false;
    s_last_activity_tick  = Time_GetTick();
    s_status              = (s_transferred_size >= s_expected_size) ? FILE_TRANSFER_COMPLETED : FILE_TRANSFER_TRANSFERRING;

    return FILE_TRANSFER_OK;
}

FileTransferResult FileTransfer_StartDownload(StorageObject destination, uint32_t expected_size) {
    if (s_status == FILE_TRANSFER_PENDING || s_status == FILE_TRANSFER_TRANSFERRING) {
        return FILE_TRANSFER_BUSY;
    }
    if (expected_size == 0u) {
        return FILE_TRANSFER_INVALID;
    }

    /* Start from a clean destination, same discipline bootloader.c uses
     * (Flash_Erase() before the first Flash_Program() of an attempt). */
    if (Storage_Erase(destination) != STORAGE_OK) {
        s_status = FILE_TRANSFER_FAILED;
        s_fault  = FILE_TRANSFER_FAULT_STORAGE;
        return FILE_TRANSFER_STORAGE_FAILURE;
    }

    s_destination          = destination;
    s_expected_size        = expected_size;
    s_transferred_size     = 0u;
    s_fault                = FILE_TRANSFER_FAULT_NONE;
    s_has_pending_chunk    = false;
    s_pending_chunk_length = 0u;
    s_pending_chunk_offset = 0u;
    s_last_activity_tick   = Time_GetTick();
    s_status               = FILE_TRANSFER_PENDING;

    return FILE_TRANSFER_OK;
}

FileTransferResult FileTransfer_SupplyChunk(const void* data, uint32_t length) {
    if (s_status != FILE_TRANSFER_PENDING && s_status != FILE_TRANSFER_TRANSFERRING) {
        return FILE_TRANSFER_INVALID;
    }
    if (s_has_pending_chunk) {
        return FILE_TRANSFER_BUSY; /* previous chunk not confirmed yet - caller must wait for Process() to drain it */
    }
    if (data == NULL || length == 0u || length > FILE_TRANSFER_MAX_CHUNK_BYTES) {
        return FILE_TRANSFER_INVALID;
    }
    if (s_transferred_size + length > s_expected_size) {
        return FILE_TRANSFER_OVERFLOW; /* rejected without touching any state, same as Storage_Read()'s range check */
    }

    memcpy(s_pending_chunk, data, length);
    s_pending_chunk_length = length;
    s_pending_chunk_offset = s_transferred_size;
    s_has_pending_chunk    = true;

    return attempt_write_pending_chunk();
}

FileTransferResult FileTransfer_Cancel(void) {
    if (s_status != FILE_TRANSFER_PENDING && s_status != FILE_TRANSFER_TRANSFERRING) {
        return FILE_TRANSFER_INVALID;
    }

    s_status            = FILE_TRANSFER_CANCELLED;
    s_fault             = FILE_TRANSFER_FAULT_CANCELLED;
    s_has_pending_chunk = false;

    return FILE_TRANSFER_OK;
}

void FileTransfer_Process(void) {
    /* One step per call, same Non-Blocking discipline as every other
     * Process() in this firmware - never drains more than one pending
     * operation. */
    if (s_has_pending_chunk) {
        (void)attempt_write_pending_chunk();
        return;
    }

    if ((s_status == FILE_TRANSFER_PENDING || s_status == FILE_TRANSFER_TRANSFERRING)
        && Time_Elapsed(s_last_activity_tick, FILE_TRANSFER_TIMEOUT_TICKS)) {
        s_status = FILE_TRANSFER_FAILED;
        s_fault  = FILE_TRANSFER_FAULT_TIMEOUT;
    }
}

FileTransferStatus FileTransfer_GetStatus(void) {
    return s_status;
}

FileTransferFault FileTransfer_GetFault(void) {
    return s_fault;
}

FileTransferProgress FileTransfer_GetProgress(void) {
    FileTransferProgress progress;
    progress.total_size       = s_expected_size;
    progress.transferred_size = s_transferred_size;
    progress.remaining_size   = s_expected_size - s_transferred_size; /* never underflows: SupplyChunk() rejects anything that would push transferred_size past expected_size */
    return progress;
}
