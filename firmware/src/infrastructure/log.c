#include "infrastructure/log.h"
#include "infrastructure/storage.h"
#include "shared/log_record.h"
#include "shared/queues.h"
#include <stddef.h>

/* Contract source: docs/03-infrastructure-contracts/log-infrastructure.md
 *
 * TODO(open-item): LOG_STORAGE_CAPACITY_BYTES (infrastructure/log.h) is a
 * placeholder, not a measured value - the real LOG Partition size is tied
 * to the still-open, blocking NOR capacity question
 * (nor-flash-infrastructure.md section 4-1, OPEN-ITEMS.md #7). Picked only
 * so the circular-region math below has a concrete number to work with;
 * must be revisited together with that decision.
 *
 * Failure handling (section 15: "a Log storage error must not stop the
 * Super Loop or a System Reset, and must not create an unbounded retry
 * chain"): unlike infrastructure/file_transfer.h (where losing a byte
 * corrupts a firmware image and is worth retrying), losing one Log Record
 * has no such consequence, so this file does not keep a pending/retry
 * state machine - Storage_Write() is tried exactly once per record; on
 * anything other than STORAGE_OK, the record is simply dropped and the
 * write offset does not advance. STORAGE_LOG has no real backend yet
 * (infrastructure/storage.c), so in production every write drops today -
 * a safe, traceable no-op, same as every other StorageObject that is
 * still a placeholder. */

static uint32_t s_write_offset;

void Log_Init(void) {
    s_write_offset = 0u;
}

void Log_Process(void) {
    LogRecord record;
    uint32_t  length;

    if (!Queue_Pop(Queues_GetLogQueue(), &record, sizeof(record), &length)) {
        return; /* nothing pending */
    }

    if (Storage_Write(STORAGE_LOG, s_write_offset, &record, sizeof(record)) != STORAGE_OK) {
        return; /* dropped - see the failure-handling note above, no retry */
    }

    s_write_offset += (uint32_t)sizeof(record);
    if (s_write_offset + (uint32_t)sizeof(record) > LOG_STORAGE_CAPACITY_BYTES) {
        s_write_offset = 0u; /* wrap - the next record starts overwriting the oldest one, approximating section 10's "oldest gets replaced" */
    }
}

uint32_t Log_GetWriteOffset(void) {
    return s_write_offset;
}
