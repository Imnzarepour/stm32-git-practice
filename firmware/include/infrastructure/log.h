#ifndef INFRA_LOG_H
#define INFRA_LOG_H
#include <stdint.h>

/* TODO(open-item): placeholder, not a measured value - see log.c's top
 * comment. Exposed here (not a local #define in log.c) so both the
 * production wrap-around math and this Infrastructure's own tests always
 * agree on the same number. */
#define LOG_STORAGE_CAPACITY_BYTES 4096u

/* Contract source: docs/03-infrastructure-contracts/log-infrastructure.md
 *
 * Log Infrastructure consumes the Log Queue (shared/queues.h) and appends
 * each LogRecord (shared/log_record.h) to STORAGE_LOG - it never decides
 * WHAT gets logged (that is entirely the producing Management's call,
 * section 2), and it never interprets a record's Source/Type/Payload.
 *
 * TEMPORARY simplification (explicit note, not a closed decision): the
 * document's real design (section 8-11) is multiple named Log Files with
 * oldest-file rotation, sitting behind a Logical File API
 * (Open/Create/Append/Rotate) on top of Storage/LittleFS - that File API
 * does not exist anywhere in this codebase yet (same gap
 * infrastructure/file_transfer.h hit - see its own top comment), and
 * building it is entangled with the still-open NOR capacity question
 * (docs/03-infrastructure-contracts/nor-flash-infrastructure.md section
 * 4-1, OPEN-ITEMS.md #7, blocking). Until that exists, this file treats
 * STORAGE_LOG as ONE fixed-capacity circular byte region (see log.c's
 * LOG_STORAGE_CAPACITY_BYTES) and simply wraps back to offset 0 once full
 * - approximating the document's "oldest gets replaced" intent (section
 * 10) without real per-file granularity. Replace this with true
 * multi-file Rotation once the File API exists; nothing above this layer
 * (the Log Queue Contract, LogRecord shape) needs to change when that
 * happens. */

void Log_Init(void);
void Log_Process(void); /* Non-Blocking - drains and appends at most one LogRecord per call (queue-infrastructure.md section 22) */

/* Non-Blocking, Read-Only - the next offset within STORAGE_LOG's circular
 * region Log_Process() will write to. Exists purely so this infrastructure
 * is traceable in a unit test and in a live debugger (same reasoning as
 * infrastructure/file_transfer.h's Traceability note) - no real caller
 * needs it. */
uint32_t Log_GetWriteOffset(void);

#endif /* INFRA_LOG_H */
