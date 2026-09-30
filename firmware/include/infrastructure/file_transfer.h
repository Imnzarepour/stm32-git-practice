#ifndef INFRA_FILE_TRANSFER_H
#define INFRA_FILE_TRANSFER_H
#include <stdint.h>
#include "infrastructure/storage.h"

/* Contract source: docs/03-infrastructure-contracts/file-transfer-infrastructure.md
 *
 * File Transfer Infrastructure moves bytes between a source (today: a
 * caller-supplied chunk - see FileTransfer_SupplyChunk()) and a
 * StorageObject destination. It never knows what the file means (section
 * 3: "File Transfer Infrastructure مسئول انتقال فایل است، نه معنای
 * فایل") - unlike the old design (where this mechanism lived inside a
 * Management named after the specific "firmware over the air" use case),
 * this Contract is deliberately generic: the same API downloads a
 * firmware image into STORAGE_FIRMWARE_UPDATE today, and could just as
 * well move a Diagnostic Dump or a Log Upload through a different
 * StorageObject tomorrow (section 19) - nothing in this file is named
 * after any one use case.
 *
 * TEMPORARY BACKEND NOTE (explicit user context): the real NOR/Memory
 * module is being built by an outside contractor and has not been
 * delivered yet (docs/05-outsourcing/nor-storage-outsourcing-spec.md) -
 * STORAGE_FIRMWARE_UPDATE in infrastructure/storage.c is still an unimplemented
 * placeholder (Storage_Write always returns STORAGE_ERROR for it). This
 * file is written entirely against the Storage Contract
 * (infrastructure/storage.h), never against NOR/SPI/LittleFS directly -
 * so once the contractor's module lands and STORAGE_FIRMWARE_UPDATE gets a real
 * backend, nothing here needs to change; every result this file returns
 * today is real and traceable (see below), it just always resolves to
 * "storage failed" until that backend exists.
 *
 * Traceability (explicit user request): every public function returns a
 * distinct, named result - never a bare bool - specifically so a wrong
 * outcome is identifiable both from a unit test assertion (TEST_ASSERT_
 * EQUAL_INT(FILE_TRANSFER_STORAGE_FAILURE, ...)) and from a live debugger
 * (FileTransferResult/FileTransferStatus/FileTransferFault are all plain
 * enums held in ordinary static state - FileTransfer_GetStatus()/
 * GetFault()/GetProgress() - never optimized away or hidden inside a
 * single opaque flag). */

typedef enum {
    FILE_TRANSFER_IDLE,        /* no transfer requested yet, or the last one finished and nothing new started */
    FILE_TRANSFER_PENDING,     /* StartDownload() accepted, waiting for the first chunk */
    FILE_TRANSFER_TRANSFERRING,
    FILE_TRANSFER_COMPLETED,
    FILE_TRANSFER_FAILED,
    FILE_TRANSFER_CANCELLED
} FileTransferStatus;

/* Section 7 of the Contract. CONNECTION is listed for Contract completeness
 * (Server Management/EC200U are not wired into this Infrastructure yet -
 * see the file's own top comment in file_transfer.c) - nothing in this
 * file raises it today. */
typedef enum {
    FILE_TRANSFER_FAULT_NONE,
    FILE_TRANSFER_FAULT_CONNECTION,
    FILE_TRANSFER_FAULT_STORAGE,
    FILE_TRANSFER_FAULT_TIMEOUT,
    FILE_TRANSFER_FAULT_INTEGRITY,
    FILE_TRANSFER_FAULT_CANCELLED
} FileTransferFault;

/* Return type of every action below - deliberately not bool, see the
 * Traceability note above. */
typedef enum {
    FILE_TRANSFER_OK,
    FILE_TRANSFER_BUSY,             /* a transfer (or a not-yet-confirmed pending chunk) is already in progress */
    FILE_TRANSFER_INVALID,          /* bad arguments, or called from a Status that does not allow this action */
    FILE_TRANSFER_OVERFLOW,         /* this chunk would write past the Expected Size given to StartDownload() */
    FILE_TRANSFER_STORAGE_FAILURE   /* the underlying Storage_Write()/Storage_GetStatus() call failed */
} FileTransferResult;

typedef struct {
    uint32_t total_size;
    uint32_t transferred_size;
    uint32_t remaining_size;
} FileTransferProgress;

/* Largest chunk this file will buffer internally while a Storage write is
 * pending (STORAGE_BUSY) - see file_transfer_config.h. A chunk larger than
 * this is rejected (FILE_TRANSFER_INVALID) rather than silently truncated. */
#include "infrastructure/file_transfer_config.h"

void FileTransfer_Init(void);
void FileTransfer_Process(void); /* Non-Blocking - advances a pending Storage write and checks the Timeout */

/* Begins a new Download into `destination` (any StorageObject - this
 * function does not care which one). Fails with FILE_TRANSFER_BUSY if a
 * transfer is already running; the caller must Cancel() or let it
 * complete/fail first. */
FileTransferResult FileTransfer_StartDownload(StorageObject destination, uint32_t expected_size);

/* The "Server Data" input from the Contract (section 6) - stands in for
 * data arriving incrementally from Server Management/EC200U, which is not
 * wired up yet (see file_transfer.c). Writes `length` bytes at the
 * current transfer offset into the destination given to StartDownload(). */
FileTransferResult FileTransfer_SupplyChunk(const void* data, uint32_t length);

FileTransferResult FileTransfer_Cancel(void);

FileTransferStatus   FileTransfer_GetStatus(void);
FileTransferFault    FileTransfer_GetFault(void);
FileTransferProgress FileTransfer_GetProgress(void);

#endif /* INFRA_FILE_TRANSFER_H */
