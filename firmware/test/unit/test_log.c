#include "unity.h"
#include "infrastructure/log.h"
#include "Mockstorage.h"
#include "shared/log_record.h"
#include "shared/queues.h"
#include <string.h>

/* Only infrastructure/storage.h is mocked (CMock) - the same seam
 * test_system_management.c and test_file_transfer.c already use. Log_Init/
 * Process() never need a real STORAGE_LOG backend to be fully testable. */
TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")

static int           s_storage_write_calls;
static StorageObject s_last_write_object;
static uint32_t      s_last_write_offset;
static uint32_t      s_last_write_length;
static LogRecord      s_last_write_record;
static bool           s_storage_write_should_fail;

static StorageResult stub_storage_write(StorageObject object, uint32_t offset, const void* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_storage_write_calls++;
    s_last_write_object = object;
    s_last_write_offset = offset;
    s_last_write_length = length;
    TEST_ASSERT_EQUAL_UINT32(sizeof(s_last_write_record), length);
    memcpy(&s_last_write_record, data, length);
    return s_storage_write_should_fail ? STORAGE_ERROR : STORAGE_OK;
}

static LogRecord make_record(LogSource source, uint32_t type) {
    LogRecord record;
    record.source         = source;
    record.type            = type;
    record.timestamp       = 1000u;
    record.payload_length = 0u;
    memset(record.payload, 0, sizeof(record.payload));
    return record;
}

void setUp(void) {
    Queues_Init();

    s_storage_write_calls       = 0;
    s_storage_write_should_fail = false;
    s_last_write_object         = (StorageObject)-1;
    s_last_write_offset         = 0xFFFFFFFFu;
    s_last_write_length         = 0u;
    memset(&s_last_write_record, 0, sizeof(s_last_write_record));

    Storage_Write_StubWithCallback(stub_storage_write);

    Log_Init();
}

void tearDown(void) {}

/* --- Init --- */

void test_init_starts_at_offset_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, Log_GetWriteOffset());
}

/* --- Process(): empty queue --- */

void test_process_with_empty_queue_does_nothing(void) {
    Log_Process();

    TEST_ASSERT_EQUAL_INT(0, s_storage_write_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, Log_GetWriteOffset());
}

/* --- Process(): one record, success --- */

void test_process_writes_the_record_at_the_current_offset(void) {
    LogRecord record = make_record(LOG_SOURCE_SYSTEM, 42u);
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetLogQueue(), &record, sizeof(record)));

    Log_Process();

    TEST_ASSERT_EQUAL_INT(1, s_storage_write_calls);
    TEST_ASSERT_EQUAL_INT(STORAGE_LOG, s_last_write_object);
    TEST_ASSERT_EQUAL_UINT32(0u, s_last_write_offset);
    TEST_ASSERT_EQUAL_INT(LOG_SOURCE_SYSTEM, s_last_write_record.source);
    TEST_ASSERT_EQUAL_UINT32(42u, s_last_write_record.type);
}

void test_process_advances_the_write_offset_by_one_record_size_on_success(void) {
    LogRecord record = make_record(LOG_SOURCE_DIAGNOSTIC, 1u);
    Queue_Push(Queues_GetLogQueue(), &record, sizeof(record));

    Log_Process();

    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(LogRecord), Log_GetWriteOffset());
}

void test_process_drains_exactly_one_record_per_call(void) {
    LogRecord a = make_record(LOG_SOURCE_SYSTEM, 1u);
    LogRecord b = make_record(LOG_SOURCE_SYSTEM, 2u);
    Queue_Push(Queues_GetLogQueue(), &a, sizeof(a));
    Queue_Push(Queues_GetLogQueue(), &b, sizeof(b));

    Log_Process();
    TEST_ASSERT_EQUAL_INT(1, s_storage_write_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, s_last_write_record.type);

    Log_Process();
    TEST_ASSERT_EQUAL_INT(2, s_storage_write_calls);
    TEST_ASSERT_EQUAL_UINT32(2u, s_last_write_record.type);
}

/* --- Process(): second record lands right after the first --- */

void test_second_record_is_written_after_the_first(void) {
    LogRecord a = make_record(LOG_SOURCE_SYSTEM, 1u);
    LogRecord b = make_record(LOG_SOURCE_SYSTEM, 2u);
    Queue_Push(Queues_GetLogQueue(), &a, sizeof(a));
    Queue_Push(Queues_GetLogQueue(), &b, sizeof(b));

    Log_Process();
    Log_Process();

    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(LogRecord), s_last_write_offset);
}

/* --- Process(): Storage failure - dropped, no retry, offset unchanged --- */

void test_storage_failure_drops_the_record_without_advancing_the_offset(void) {
    s_storage_write_should_fail = true;
    LogRecord record = make_record(LOG_SOURCE_SYSTEM, 1u);
    Queue_Push(Queues_GetLogQueue(), &record, sizeof(record));

    Log_Process();

    TEST_ASSERT_EQUAL_INT(1, s_storage_write_calls);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Log_GetWriteOffset(), "a failed write must not advance the offset");
}

void test_storage_failure_does_not_retry_the_same_record(void) {
    s_storage_write_should_fail = true;
    LogRecord failing = make_record(LOG_SOURCE_SYSTEM, 1u);
    Queue_Push(Queues_GetLogQueue(), &failing, sizeof(failing));

    Log_Process(); /* dropped - see log.c's failure-handling note: no unbounded retry chain (section 15) */
    TEST_ASSERT_EQUAL_INT(1, s_storage_write_calls);

    s_storage_write_should_fail = false;
    LogRecord next = make_record(LOG_SOURCE_SYSTEM, 2u);
    Queue_Push(Queues_GetLogQueue(), &next, sizeof(next));

    Log_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_storage_write_calls, "Process() must move on to the next queued record, not retry the dropped one");
    TEST_ASSERT_EQUAL_UINT32(2u, s_last_write_record.type);
}

/* --- Process(): wrap-around, section 10's "oldest gets replaced" --- */

void test_write_offset_wraps_back_to_zero_once_capacity_is_reached(void) {
    uint32_t records_until_wrap = LOG_STORAGE_CAPACITY_BYTES / (uint32_t)sizeof(LogRecord);
    uint32_t i;

    for (i = 0; i < records_until_wrap; i++) {
        LogRecord record = make_record(LOG_SOURCE_SYSTEM, i);
        Queue_Push(Queues_GetLogQueue(), &record, sizeof(record));
        Log_Process();
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Log_GetWriteOffset(), "offset must wrap back to 0 once the next record would not fit");

    /* the very next record must land at offset 0 again - i.e. it starts
     * overwriting whatever the oldest record there was. */
    LogRecord wrapped = make_record(LOG_SOURCE_SYSTEM, 999u);
    Queue_Push(Queues_GetLogQueue(), &wrapped, sizeof(wrapped));
    Log_Process();

    TEST_ASSERT_EQUAL_UINT32(0u, s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(999u, s_last_write_record.type);
}
