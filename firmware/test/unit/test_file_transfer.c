#include "unity.h"
#include "infrastructure/file_transfer.h"
#include "Mockstorage.h"
#include "infrastructure/time_base.h"
#include <string.h>
#include <stdint.h>

TEST_SOURCE_FILE("infrastructure/time_base.c")

/* Covers file-transfer-infrastructure.md's Download path. Only
 * infrastructure/storage.h is mocked (CMock), the same seam
 * test_system_management.c already uses for STORAGE_RECOVERY - this file
 * never needs a real NOR/contractor module to be testable (see
 * file_transfer.c's own top comment). Every assertion below checks a
 * named FileTransferResult/Status/Fault value, never a bare bool, so a
 * wrong outcome is identifiable both here and by inspecting
 * FileTransfer_GetStatus()/GetFault() live in a debugger. */

static StorageResult s_stub_storage_erase_result;
static int           s_storage_erase_calls;
static StorageObject s_last_erase_object;

static StorageResult s_storage_write_results[4];
static int            s_storage_write_result_count;
static int            s_storage_write_calls;
static StorageObject  s_last_write_object;
static uint32_t       s_last_write_offset;
static uint32_t       s_last_write_length;
static uint8_t        s_last_write_data[FILE_TRANSFER_MAX_CHUNK_BYTES];

static StorageResult stub_storage_erase(StorageObject object, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_storage_erase_calls++;
    s_last_erase_object = object;
    return s_stub_storage_erase_result;
}

static StorageResult stub_storage_write(StorageObject object, uint32_t offset, const void* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_storage_write_calls++;
    s_last_write_object = object;
    s_last_write_offset = offset;
    s_last_write_length = length;
    TEST_ASSERT_TRUE(length <= sizeof(s_last_write_data));
    memcpy(s_last_write_data, data, length);

    if (s_storage_write_calls <= s_storage_write_result_count) {
        return s_storage_write_results[s_storage_write_calls - 1];
    }
    return STORAGE_OK; /* default once the queued results run out */
}

/* Queues the result stub_storage_write() returns on its Nth call (in
 * call order) - used to script BUSY-then-OK / permanent-failure
 * sequences without needing a real Storage backend. */
static void queue_write_result(StorageResult result) {
    TEST_ASSERT_TRUE(s_storage_write_result_count < (int)(sizeof(s_storage_write_results) / sizeof(s_storage_write_results[0])));
    s_storage_write_results[s_storage_write_result_count] = result;
    s_storage_write_result_count++;
}

void setUp(void) {
    Time_Init();

    s_stub_storage_erase_result = STORAGE_OK;
    s_storage_erase_calls       = 0;

    s_storage_write_calls        = 0;
    s_storage_write_result_count = 0;
    memset(s_storage_write_results, 0, sizeof(s_storage_write_results));
    memset(s_last_write_data, 0, sizeof(s_last_write_data));

    Storage_Erase_StubWithCallback(stub_storage_erase);
    Storage_Write_StubWithCallback(stub_storage_write);

    FileTransfer_Init();
}

void tearDown(void) {}

/* --- Init --- */

void test_init_starts_idle_with_no_fault(void) {
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_IDLE, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_FAULT_NONE, FileTransfer_GetFault());
}

/* --- StartDownload --- */

void test_start_download_erases_destination_and_enters_pending(void) {
    FileTransferResult result = FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_OK, result);
    TEST_ASSERT_EQUAL_INT(1, s_storage_erase_calls);
    TEST_ASSERT_EQUAL_INT(STORAGE_FIRMWARE_UPDATE, s_last_erase_object);
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_PENDING, FileTransfer_GetStatus());

    FileTransferProgress progress = FileTransfer_GetProgress();
    TEST_ASSERT_EQUAL_UINT32(10u, progress.total_size);
    TEST_ASSERT_EQUAL_UINT32(0u, progress.transferred_size);
    TEST_ASSERT_EQUAL_UINT32(10u, progress.remaining_size);
}

void test_start_download_rejects_zero_expected_size(void) {
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_INVALID, FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 0u));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_storage_erase_calls, "must not touch Storage for a rejected request");
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_IDLE, FileTransfer_GetStatus());
}

void test_start_download_reports_storage_failure_when_erase_fails(void) {
    s_stub_storage_erase_result = STORAGE_ERROR;

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_STORAGE_FAILURE, FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u));
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_FAILED, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_FAULT_STORAGE, FileTransfer_GetFault());
}

void test_start_download_while_already_running_is_busy(void) {
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_BUSY, FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 5u));
}

/* --- SupplyChunk --- */

void test_supply_chunk_before_start_is_invalid(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_INVALID, FileTransfer_SupplyChunk(chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL_INT(0, s_storage_write_calls);
}

void test_supply_chunk_writes_and_advances_progress(void) {
    uint8_t chunk[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);

    FileTransferResult result = FileTransfer_SupplyChunk(chunk, sizeof(chunk));

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_OK, result);
    TEST_ASSERT_EQUAL_INT(1, s_storage_write_calls);
    TEST_ASSERT_EQUAL_INT(STORAGE_FIRMWARE_UPDATE, s_last_write_object);
    TEST_ASSERT_EQUAL_UINT32(0u, s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(4u, s_last_write_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk, s_last_write_data, 4);
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_TRANSFERRING, FileTransfer_GetStatus());

    FileTransferProgress progress = FileTransfer_GetProgress();
    TEST_ASSERT_EQUAL_UINT32(4u, progress.transferred_size);
    TEST_ASSERT_EQUAL_UINT32(6u, progress.remaining_size);
}

void test_second_chunk_is_written_at_the_correct_offset(void) {
    uint8_t chunk1[4] = { 1, 2, 3, 4 };
    uint8_t chunk2[3] = { 5, 6, 7 };
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);
    FileTransfer_SupplyChunk(chunk1, sizeof(chunk1));

    FileTransfer_SupplyChunk(chunk2, sizeof(chunk2));

    TEST_ASSERT_EQUAL_UINT32(4u, s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(7u, FileTransfer_GetProgress().transferred_size);
}

void test_supply_chunk_completes_transfer_when_reaching_expected_size(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 4u);

    FileTransfer_SupplyChunk(chunk, sizeof(chunk));

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_COMPLETED, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_UINT32(0u, FileTransfer_GetProgress().remaining_size);
}

void test_supply_chunk_rejects_overflow_without_writing(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 2u);

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_OVERFLOW, FileTransfer_SupplyChunk(chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_storage_write_calls, "an overflowing chunk must never reach Storage");
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_PENDING, FileTransfer_GetStatus());
}

void test_supply_chunk_rejects_null_data(void) {
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_INVALID, FileTransfer_SupplyChunk(NULL, 4u));
}

void test_supply_chunk_rejects_zero_length(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_INVALID, FileTransfer_SupplyChunk(chunk, 0u));
}

void test_supply_chunk_rejects_a_chunk_larger_than_the_internal_buffer(void) {
    uint8_t big[FILE_TRANSFER_MAX_CHUNK_BYTES + 1];
    memset(big, 0, sizeof(big));
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, sizeof(big));

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_INVALID, FileTransfer_SupplyChunk(big, sizeof(big)));
    TEST_ASSERT_EQUAL_INT(0, s_storage_write_calls);
}

void test_supply_chunk_reports_storage_failure(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    queue_write_result(STORAGE_ERROR);
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_STORAGE_FAILURE, FileTransfer_SupplyChunk(chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_FAILED, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_FAULT_STORAGE, FileTransfer_GetFault());
}

void test_supply_chunk_is_busy_while_a_previous_chunk_is_still_pending(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    queue_write_result(STORAGE_BUSY);
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);
    FileTransfer_SupplyChunk(chunk, sizeof(chunk)); /* accepted, but Storage has not confirmed it yet */

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_BUSY, FileTransfer_SupplyChunk(chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_storage_write_calls, "the second call must not reach Storage while one is still pending");
}

/* --- Process(): retry + Timeout --- */

void test_process_retries_a_busy_chunk_until_storage_confirms_it(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    queue_write_result(STORAGE_BUSY);
    queue_write_result(STORAGE_OK);
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 4u);
    FileTransfer_SupplyChunk(chunk, sizeof(chunk));
    TEST_ASSERT_EQUAL_INT_MESSAGE(FILE_TRANSFER_PENDING, FileTransfer_GetStatus(), "must not advance until Storage confirms the write");

    FileTransfer_Process();

    TEST_ASSERT_EQUAL_INT(2, s_storage_write_calls);
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_COMPLETED, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_UINT32(4u, FileTransfer_GetProgress().transferred_size);
}

void test_process_does_nothing_when_idle(void) {
    FileTransfer_Process();
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_IDLE, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_INT(0, s_storage_write_calls);
}

void test_process_times_out_a_stalled_transfer(void) {
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);

    uint32_t i;
    for (i = 0; i <= FILE_TRANSFER_TIMEOUT_TICKS; i++) {
        Time_TickIncrement();
    }
    FileTransfer_Process();

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_FAILED, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_FAULT_TIMEOUT, FileTransfer_GetFault());
}

void test_process_before_timeout_does_not_fail_the_transfer(void) {
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);

    /* One tick short of the threshold - Time_Elapsed() triggers at
     * exactly ">= duration" (see test_process_times_out_a_stalled_transfer
     * above, which reaches the threshold exactly), so this must land one
     * tick earlier to prove the boundary is not off by one. */
    uint32_t i;
    for (i = 0; i < FILE_TRANSFER_TIMEOUT_TICKS - 1u; i++) {
        Time_TickIncrement();
    }
    FileTransfer_Process();

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_PENDING, FileTransfer_GetStatus());
}

/* --- Cancel --- */

void test_cancel_stops_an_active_transfer(void) {
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_OK, FileTransfer_Cancel());
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_CANCELLED, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_FAULT_CANCELLED, FileTransfer_GetFault());
}

void test_cancel_when_idle_is_invalid(void) {
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_INVALID, FileTransfer_Cancel());
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_IDLE, FileTransfer_GetStatus());
}

void test_cancel_discards_a_pending_unconfirmed_chunk(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    queue_write_result(STORAGE_BUSY);
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 10u);
    FileTransfer_SupplyChunk(chunk, sizeof(chunk));

    FileTransfer_Cancel();
    FileTransfer_Process(); /* must not resurrect the cancelled transfer */

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_CANCELLED, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_storage_write_calls, "Process() must not retry a chunk that was cancelled");
}

/* --- Next transfer after one finishes --- */

void test_a_new_download_can_start_after_the_previous_one_completed(void) {
    uint8_t chunk[4] = { 1, 2, 3, 4 };
    FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 4u);
    FileTransfer_SupplyChunk(chunk, sizeof(chunk));
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_COMPLETED, FileTransfer_GetStatus());

    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_OK, FileTransfer_StartDownload(STORAGE_FIRMWARE_UPDATE, 20u));
    TEST_ASSERT_EQUAL_INT(FILE_TRANSFER_PENDING, FileTransfer_GetStatus());
    TEST_ASSERT_EQUAL_UINT32(0u, FileTransfer_GetProgress().transferred_size);
}
