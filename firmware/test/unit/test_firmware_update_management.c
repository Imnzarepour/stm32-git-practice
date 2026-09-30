#include "unity.h"
#include "managements/firmware_update_management.h"
#include "managements/firmware_update_management_config.h"
#include "infrastructure/time_base.h"
#include "infrastructure/storage.h"
#include "infrastructure/file_transfer_config.h"
#include "shared/reset_request.h"
#include "shared/event.h"
#include "shared/queues.h"
#include "Mockfile_transfer.h"
#include "Mockpower_management.h"
#include "Mockstorage.h"
#include "Mockreset_request.h"
#include "Mockec200u_manager.h"
#include <string.h>

#define FIRMWARE_TEST_URL "https://example.com/fw.bin"

TEST_SOURCE_FILE("infrastructure/time_base.c")
TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")

/* Firmware Update Management's real dependencies are File Transfer
 * Infrastructure, EC200U Manager's HTTP Client Capability (the actual
 * source of download bytes now, since FOTA-over-HTTP was confirmed -
 * OPEN-ITEMS.md TCU<->Backend #12) and Power Management's Power_GetMode()
 * (the LOW_POWER check-interval throttle - see the source file's own
 * comment) - all Mocked here so this test isolates its own state machine
 * (IDLE -> REQUESTED -> DOWNLOADING -> READY/FAILED) from File Transfer's
 * own chunk/timeout mechanics (test_file_transfer.c's job) and from
 * EC200U's own simulated HTTP behavior (test_ec200u_manager.c's job, if
 * one exists). Time Base is real (time_base.c) so the throttle timeline
 * can be driven deterministically with Time_TickIncrement(), same pattern
 * test_gps_management.c/test_call_management.c already use for their own
 * LOW_POWER pacing tests.
 * See the header/source file comments for the exact scope (explicit user
 * decision, two parts, tracked in OPEN-ITEMS.md): Download + Store only
 * (this file's original tests below), plus FirmwareUpdate_SubmitInstallRequest()'s
 * VALIDATING -> INSTALLING transition (Storage Read/Write on STORAGE_RECOVERY
 * + a real Reset Request - both Mocked here via Mockstorage.h/
 * Mockreset_request.h, same reasoning as File Transfer/Power Management
 * above: this test isolates the state machine from what Storage/System
 * Management actually do). */

static FileTransferResult s_stub_start_download_result;
static int                 s_start_download_call_count;
static StorageObject        s_last_destination;
static uint32_t             s_last_expected_size;

static FileTransferResult stub_start_download(StorageObject destination, uint32_t expected_size, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_start_download_call_count++;
    s_last_destination    = destination;
    s_last_expected_size  = expected_size;
    return s_stub_start_download_result;
}

static FileTransferStatus s_stub_transfer_status;
static FileTransferStatus stub_get_status(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_transfer_status;
}

static int                s_supply_chunk_call_count;
static FileTransferResult s_stub_supply_chunk_result;
static uint8_t             s_last_supplied_chunk[FILE_TRANSFER_MAX_CHUNK_BYTES];
static uint32_t            s_last_supplied_chunk_length;

static FileTransferResult stub_supply_chunk(const void* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_supply_chunk_call_count++;
    s_last_supplied_chunk_length = (length <= sizeof(s_last_supplied_chunk)) ? length : sizeof(s_last_supplied_chunk);
    if (data) {
        memcpy(s_last_supplied_chunk, data, s_last_supplied_chunk_length);
    }
    return s_stub_supply_chunk_result;
}

/* --- EC200U HTTP Client Capability (infrastructure/ec200u_manager.h) -
 * see firmware_update_management.c's pull_http_chunks()/FirmwareUpdate_Process()
 * for how these three are actually used: StartGet() once per Download,
 * then GetState()/GetChunk() polled every single Process() call while
 * DOWNLOADING (unthrottled - see that function's own comment). --- */
static int               s_http_start_get_call_count;
static char               s_last_http_url[FIRMWARE_DOWNLOAD_URL_MAX_LENGTH];
static EC200U_Result      s_stub_http_start_get_result;
static EC200U_HttpState   s_stub_http_state;
static EC200U_Result      s_stub_http_get_chunk_result;
static uint8_t             s_stub_http_chunk_data[FILE_TRANSFER_MAX_CHUNK_BYTES];
static uint32_t            s_stub_http_chunk_length;

static EC200U_Result stub_http_start_get(const char* url, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_http_start_get_call_count++;
    if (url) strncpy(s_last_http_url, url, sizeof(s_last_http_url) - 1u);
    return s_stub_http_start_get_result;
}

static EC200U_HttpState stub_http_get_state(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_http_state;
}

static EC200U_Result stub_http_get_chunk(uint8_t* buffer, uint32_t buffer_size, uint32_t* received_length, int cmock_num_calls) {
    (void)cmock_num_calls;
    if (s_stub_http_get_chunk_result != EC200U_OK) {
        if (received_length) *received_length = 0u;
        return s_stub_http_get_chunk_result;
    }
    TEST_ASSERT_TRUE(s_stub_http_chunk_length <= buffer_size);
    memcpy(buffer, s_stub_http_chunk_data, s_stub_http_chunk_length);
    if (received_length) *received_length = s_stub_http_chunk_length;
    return EC200U_OK;
}

static PowerMode s_stub_power_mode;
static PowerMode stub_power_get_mode(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_power_mode;
}

/* --- Storage_Read/Write on STORAGE_RECOVERY - two independent sub-records
 * share this StorageObject (see firmware_update_management.c's own
 * comments): FirmwareUpdateBootControl at offset 0 (Bootloader-owned,
 * FirmwareUpdate_SubmitInstallRequest's Boot Action write) and
 * FirmwareUpdatePendingInstallRecord at offset 16 (owned entirely by this
 * Management, FirmwareUpdate_Init()'s "did the Bank actually switch"
 * check). The stub below dispatches on offset so both are exercised
 * correctly in the same test file. */

typedef struct {
    uint32_t active_bank;
    uint32_t action;
} TestBootControl;

typedef struct {
    uint32_t pending;
    uint32_t previous_active_bank;
} TestPendingInstall;

#define TEST_BOOT_CONTROL_OFFSET     0u
#define TEST_PENDING_INSTALL_OFFSET 16u

static StorageResult      s_stub_storage_read_result;
static TestBootControl    s_stub_boot_control_on_disk;
static TestPendingInstall s_stub_pending_install_on_disk;
static StorageObject       s_last_read_object;

static StorageResult stub_storage_read(StorageObject object, uint32_t offset, void* buffer, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_last_read_object = object;
    if (s_stub_storage_read_result != STORAGE_OK) {
        return s_stub_storage_read_result;
    }
    if (offset == TEST_BOOT_CONTROL_OFFSET) {
        TEST_ASSERT_EQUAL_UINT32(sizeof(TestBootControl), length);
        memcpy(buffer, &s_stub_boot_control_on_disk, sizeof(TestBootControl));
    } else if (offset == TEST_PENDING_INSTALL_OFFSET) {
        TEST_ASSERT_EQUAL_UINT32(sizeof(TestPendingInstall), length);
        memcpy(buffer, &s_stub_pending_install_on_disk, sizeof(TestPendingInstall));
    } else {
        TEST_FAIL_MESSAGE("unexpected Storage_Read offset on STORAGE_RECOVERY");
    }
    return STORAGE_OK;
}

static StorageResult      s_stub_storage_write_result;
static int                 s_storage_write_call_count;
static StorageObject       s_last_write_object;
static TestBootControl     s_last_written_boot_control;
static TestPendingInstall  s_last_written_pending_install;

static StorageResult stub_storage_write(StorageObject object, uint32_t offset, const void* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_storage_write_call_count++;
    s_last_write_object = object;
    if (offset == TEST_BOOT_CONTROL_OFFSET) {
        TEST_ASSERT_EQUAL_UINT32(sizeof(TestBootControl), length);
        memcpy(&s_last_written_boot_control, data, sizeof(TestBootControl));
    } else if (offset == TEST_PENDING_INSTALL_OFFSET) {
        TEST_ASSERT_EQUAL_UINT32(sizeof(TestPendingInstall), length);
        memcpy(&s_last_written_pending_install, data, sizeof(TestPendingInstall));
    } else {
        TEST_FAIL_MESSAGE("unexpected Storage_Write offset on STORAGE_RECOVERY");
    }
    return s_stub_storage_write_result;
}

static bool pop_event(Event* out) {
    uint32_t length;
    return Queue_Pop(Queues_GetEventQueue(), out, sizeof(*out), &length);
}

static int          s_reset_request_call_count;
static ResetRequest s_last_reset_request;

static void stub_submit_reset_request(const ResetRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_reset_request_call_count++;
    if (request) s_last_reset_request = *request;
}

static void advance_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        Time_TickIncrement();
    }
}

void setUp(void) {
    Time_Init();
    Queues_Init();

    s_stub_start_download_result = FILE_TRANSFER_OK;
    s_start_download_call_count  = 0;
    s_last_destination           = STORAGE_CONFIG;
    s_last_expected_size         = 0;
    FileTransfer_StartDownload_StubWithCallback(stub_start_download);

    s_stub_transfer_status = FILE_TRANSFER_IDLE;
    FileTransfer_GetStatus_StubWithCallback(stub_get_status);

    s_supply_chunk_call_count    = 0;
    s_stub_supply_chunk_result   = FILE_TRANSFER_OK;
    s_last_supplied_chunk_length = 0u;
    memset(s_last_supplied_chunk, 0, sizeof(s_last_supplied_chunk));
    FileTransfer_SupplyChunk_StubWithCallback(stub_supply_chunk);

    s_http_start_get_call_count  = 0;
    memset(s_last_http_url, 0, sizeof(s_last_http_url));
    s_stub_http_start_get_result = EC200U_OK;
    EC200U_HTTP_StartGet_StubWithCallback(stub_http_start_get);

    s_stub_http_state = EC200U_HTTP_IN_PROGRESS; /* default: no bytes ready yet, nothing to drain - most tests below never reach DOWNLOADING's chunk-pulling anyway */
    EC200U_HTTP_GetState_StubWithCallback(stub_http_get_state);

    s_stub_http_get_chunk_result = EC200U_NOT_READY; /* default: nothing buffered - see EC200U_HTTP_GetChunk()'s own contract */
    s_stub_http_chunk_length     = 0u;
    memset(s_stub_http_chunk_data, 0, sizeof(s_stub_http_chunk_data));
    EC200U_HTTP_GetChunk_StubWithCallback(stub_http_get_chunk);

    /* Default: NORMAL, so every pre-existing test (none of which cares
     * about Power Mode) keeps behaving exactly as before - only the
     * dedicated LOW_POWER tests below override this. */
    s_stub_power_mode = POWER_NORMAL;
    Power_GetMode_StubWithCallback(stub_power_get_mode);

    s_stub_storage_read_result       = STORAGE_OK;
    s_stub_boot_control_on_disk.active_bank = 0u; /* BANK_1 - arbitrary, only used to assert it's preserved */
    s_stub_boot_control_on_disk.action      = 0u; /* BOOT_ACTION_BOOT */
    /* Default: no install was pending - an ordinary boot, so
     * FirmwareUpdate_Init() below (called last in this function, after
     * every mock is wired) is a safe no-op for every pre-existing test.
     * Only the dedicated tests below override this before re-calling
     * FirmwareUpdate_Init() themselves to simulate "the next boot". */
    s_stub_pending_install_on_disk.pending               = 0u;
    s_stub_pending_install_on_disk.previous_active_bank  = 0u;
    s_last_read_object                 = STORAGE_CONFIG;
    Storage_Read_StubWithCallback(stub_storage_read);

    s_stub_storage_write_result   = STORAGE_OK;
    s_storage_write_call_count     = 0;
    s_last_write_object             = STORAGE_CONFIG;
    memset(&s_last_written_boot_control, 0, sizeof(s_last_written_boot_control));
    memset(&s_last_written_pending_install, 0, sizeof(s_last_written_pending_install));
    Storage_Write_StubWithCallback(stub_storage_write);

    s_reset_request_call_count = 0;
    memset(&s_last_reset_request, 0, sizeof(s_last_reset_request));
    System_SubmitResetRequest_StubWithCallback(stub_submit_reset_request);

    /* Called last, now that every mock above is wired - FirmwareUpdate_Init()
     * itself performs a real Storage_Read (FirmwareUpdate_Init()'s new
     * check_install_completed() call, see firmware_update_management.h's
     * "SCOPE, PART 3"), so it must not run before Storage_Read/Write have a
     * stub to call into. */
    FirmwareUpdate_Init();
}

void tearDown(void) {}

void test_initial_state_is_idle(void) {
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_IDLE, FirmwareUpdate_GetState());
}

void test_submit_request_rejects_zero_expected_size(void) {
    TEST_ASSERT_FALSE(FirmwareUpdate_SubmitRequest(0u, FIRMWARE_TEST_URL));
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_IDLE, FirmwareUpdate_GetState());
}

void test_submit_request_rejects_null_url(void) {
    TEST_ASSERT_FALSE(FirmwareUpdate_SubmitRequest(2048u, NULL));
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_IDLE, FirmwareUpdate_GetState());
}

void test_submit_request_rejects_empty_url(void) {
    TEST_ASSERT_FALSE(FirmwareUpdate_SubmitRequest(2048u, ""));
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_IDLE, FirmwareUpdate_GetState());
}

void test_submit_request_accepts_a_valid_size_and_moves_to_requested(void) {
    TEST_ASSERT_TRUE(FirmwareUpdate_SubmitRequest(1024u, FIRMWARE_TEST_URL));
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_REQUESTED, FirmwareUpdate_GetState());
}

void test_process_starts_the_download_against_storage_firmware_update(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(1, s_start_download_call_count);
    TEST_ASSERT_EQUAL_INT(STORAGE_FIRMWARE_UPDATE, s_last_destination);
    TEST_ASSERT_EQUAL_UINT32(2048u, s_last_expected_size);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_http_start_get_call_count,
        "EC200U_HTTP_StartGet() must be called once File Transfer's own side has started");
    TEST_ASSERT_EQUAL_STRING(FIRMWARE_TEST_URL, s_last_http_url);
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_DOWNLOADING, FirmwareUpdate_GetState());
}

void test_process_fails_when_http_start_get_fails(void) {
    s_stub_http_start_get_result = EC200U_ERROR;
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState(),
        "File Transfer's own side already started, but the HTTP side failed to even begin - must not silently claim DOWNLOADING");
}

/* --- HTTP chunk pulling (pull_http_chunks(), runs every Process() call
 * while DOWNLOADING, unthrottled - see that function's own comment) --- */

void test_downloading_pulls_an_available_http_chunk_into_file_transfer(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */

    s_stub_http_state             = EC200U_HTTP_COMPLETE;
    s_stub_http_get_chunk_result  = EC200U_OK;
    s_stub_http_chunk_length      = 4u;
    memcpy(s_stub_http_chunk_data, "\xDE\xAD\xBE\xEF", 4u);

    FirmwareUpdate_Process(); /* no throttle interval needs to elapse - chunk pulling is unthrottled */

    TEST_ASSERT_EQUAL_INT(1, s_supply_chunk_call_count);
    TEST_ASSERT_EQUAL_UINT32(4u, s_last_supplied_chunk_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("\xDE\xAD\xBE\xEF", s_last_supplied_chunk, 4u);
    TEST_ASSERT_EQUAL_INT_MESSAGE(FIRMWARE_UPDATE_DOWNLOADING, FirmwareUpdate_GetState(),
        "one chunk arriving does not by itself complete the transfer - FileTransfer_GetStatus() still decides that");
}

void test_downloading_does_not_pull_a_chunk_while_still_in_progress(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */

    s_stub_http_state = EC200U_HTTP_IN_PROGRESS; /* still fetching - GetChunk() default (NOT_READY) applies */

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(0, s_supply_chunk_call_count);
}

void test_downloading_fails_when_http_get_reports_error(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */

    s_stub_http_state = EC200U_HTTP_ERROR;

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_supply_chunk_call_count, "must never attempt to pull a chunk once EC200U itself reports an error");
}

void test_downloading_fails_when_file_transfer_rejects_a_supplied_chunk(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */

    s_stub_http_state            = EC200U_HTTP_COMPLETE;
    s_stub_http_get_chunk_result = EC200U_OK;
    s_stub_http_chunk_length     = 1u;
    s_stub_supply_chunk_result   = FILE_TRANSFER_STORAGE_FAILURE;

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState());
}

void test_chunk_pulling_is_not_throttled_by_the_check_interval(void) {
    /* Unlike FileTransfer_StartDownload()/GetStatus() (throttled below),
     * pulling already-available bytes must happen on EVERY Process() call -
     * see pull_http_chunks()'s own comment for why (Power-Mode pacing must
     * never silently cap real download throughput). */
    s_stub_power_mode = POWER_LOW_POWER;
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING, first check consumed immediately */

    s_stub_http_state            = EC200U_HTTP_COMPLETE;
    s_stub_http_get_chunk_result = EC200U_OK;
    s_stub_http_chunk_length     = 1u;

    /* No advance_ms() at all - even LOW_POWER's widest interval must not
     * delay this. */
    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(1, s_supply_chunk_call_count);
}

void test_process_retries_start_download_while_file_transfer_is_busy(void) {
    s_stub_start_download_result = FILE_TRANSFER_BUSY;
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);

    FirmwareUpdate_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(FIRMWARE_UPDATE_REQUESTED, FirmwareUpdate_GetState(),
        "BUSY must be retried on a later Process() call, not treated as a failure");

    s_stub_start_download_result = FILE_TRANSFER_OK;
    advance_ms(FIRMWARE_UPDATE_CHECK_INTERVAL_MS); /* clear the check-interval throttle so the retry is actually attempted */
    FirmwareUpdate_Process();
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_DOWNLOADING, FirmwareUpdate_GetState());
}

void test_process_fails_when_start_download_reports_storage_failure(void) {
    s_stub_start_download_result = FILE_TRANSFER_STORAGE_FAILURE; /* the expected outcome today - STORAGE_FIRMWARE_UPDATE has no real NOR backend yet */
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState());
}

void test_process_moves_to_ready_once_file_transfer_completes(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */

    s_stub_transfer_status = FILE_TRANSFER_COMPLETED;
    advance_ms(FIRMWARE_UPDATE_CHECK_INTERVAL_MS);
    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(FIRMWARE_UPDATE_READY, FirmwareUpdate_GetState(),
        "a completed Download parks in READY - no Validate/Install in this pass");
}

void test_process_fails_when_file_transfer_fails(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */

    s_stub_transfer_status = FILE_TRANSFER_FAILED;
    advance_ms(FIRMWARE_UPDATE_CHECK_INTERVAL_MS);
    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState());
}

void test_process_fails_when_file_transfer_is_cancelled(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */

    s_stub_transfer_status = FILE_TRANSFER_CANCELLED;
    advance_ms(FIRMWARE_UPDATE_CHECK_INTERVAL_MS);
    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState());
}

void test_submit_request_rejects_a_second_request_while_downloading(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */

    TEST_ASSERT_FALSE(FirmwareUpdate_SubmitRequest(4096u, FIRMWARE_TEST_URL));
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_DOWNLOADING, FirmwareUpdate_GetState());
}

void test_a_new_request_is_accepted_again_once_ready(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process();
    s_stub_transfer_status = FILE_TRANSFER_COMPLETED;
    advance_ms(FIRMWARE_UPDATE_CHECK_INTERVAL_MS);
    FirmwareUpdate_Process();
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_READY, FirmwareUpdate_GetState());

    TEST_ASSERT_TRUE(FirmwareUpdate_SubmitRequest(4096u, FIRMWARE_TEST_URL));
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_REQUESTED, FirmwareUpdate_GetState());
}

void test_a_new_request_is_accepted_again_after_a_failure(void) {
    s_stub_start_download_result = FILE_TRANSFER_STORAGE_FAILURE;
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process();
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState());

    s_stub_start_download_result = FILE_TRANSFER_OK;
    TEST_ASSERT_TRUE(FirmwareUpdate_SubmitRequest(4096u, FIRMWARE_TEST_URL));
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_REQUESTED, FirmwareUpdate_GetState());
}

/* --- Power-Mode-aware pacing (explicit user decision, OPEN-ITEMS.md:
 * "همه تجهیزات باید در حالت کم‌مصرف نرخ دیگری داشته باشند") --- */

void test_a_submitted_request_is_checked_immediately_even_in_low_power(void) {
    /* The very first check after SubmitRequest() must never wait out a
     * stale interval - see FirmwareUpdate_SubmitRequest()'s own comment. */
    s_stub_power_mode = POWER_LOW_POWER;
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(1, s_start_download_call_count);
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_DOWNLOADING, FirmwareUpdate_GetState());
}

void test_low_power_widens_the_check_interval(void) {
    s_stub_power_mode = POWER_LOW_POWER;
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING, first check consumed immediately */

    s_stub_transfer_status = FILE_TRANSFER_COMPLETED;

    advance_ms(FIRMWARE_UPDATE_CHECK_INTERVAL_MS); /* enough in NORMAL, NOT enough in LOW_POWER */
    FirmwareUpdate_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(FIRMWARE_UPDATE_DOWNLOADING, FirmwareUpdate_GetState(),
        "LOW_POWER must check less often than NORMAL - this call must still be throttled");

    advance_ms((FIRMWARE_UPDATE_CHECK_INTERVAL_MS * FIRMWARE_UPDATE_LOW_POWER_RATE_MULTIPLIER)
               - FIRMWARE_UPDATE_CHECK_INTERVAL_MS);
    FirmwareUpdate_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(FIRMWARE_UPDATE_READY, FirmwareUpdate_GetState(),
        "once the full LOW_POWER interval has elapsed, the check must go through");
}

void test_normal_power_mode_checks_at_the_base_interval(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING, first check consumed immediately */

    s_stub_transfer_status = FILE_TRANSFER_COMPLETED;

    advance_ms(FIRMWARE_UPDATE_CHECK_INTERVAL_MS);
    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(FIRMWARE_UPDATE_READY, FirmwareUpdate_GetState(),
        "NORMAL Power Mode must check again once the base interval elapses");
}

/* --- FirmwareUpdate_SubmitInstallRequest() / VALIDATING -> INSTALLING
 * (explicit user decision, given by direct analogy to the Door Lock/Unlock
 * precedent - see firmware_update_management.h's "SCOPE, PART 2") --- */

static void make_ready(void) {
    FirmwareUpdate_SubmitRequest(2048u, FIRMWARE_TEST_URL);
    FirmwareUpdate_Process(); /* -> DOWNLOADING */
    s_stub_transfer_status = FILE_TRANSFER_COMPLETED;
    advance_ms(FIRMWARE_UPDATE_CHECK_INTERVAL_MS);
    FirmwareUpdate_Process(); /* -> READY */
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_READY, FirmwareUpdate_GetState());
}

void test_submit_install_request_rejected_when_not_ready(void) {
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_IDLE, FirmwareUpdate_GetState());

    TEST_ASSERT_FALSE(FirmwareUpdate_SubmitInstallRequest());
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_IDLE, FirmwareUpdate_GetState());
}

void test_submit_install_request_accepted_when_ready_and_moves_to_validating(void) {
    make_ready();

    TEST_ASSERT_TRUE(FirmwareUpdate_SubmitInstallRequest());
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_VALIDATING, FirmwareUpdate_GetState());
}

void test_process_validating_writes_boot_action_program_and_preserves_active_bank(void) {
    make_ready();
    s_stub_boot_control_on_disk.active_bank = 1u; /* BANK_2 - whatever Bootloader last left there */
    FirmwareUpdate_SubmitInstallRequest();

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(STORAGE_RECOVERY, s_last_read_object);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_storage_write_call_count,
        "BootControl (offset 0) AND the PendingInstallRecord (offset 16) must both be written");
    TEST_ASSERT_EQUAL_INT(STORAGE_RECOVERY, s_last_write_object);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, s_last_written_boot_control.active_bank,
        "active_bank must be preserved verbatim - Bootloader is its only owner");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, s_last_written_boot_control.action,
        "action must become BOOT_ACTION_PROGRAM (1)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, s_last_written_pending_install.pending,
        "must record that an install Reset is now pending, for FirmwareUpdate_Init() to check on the next boot");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, s_last_written_pending_install.previous_active_bank,
        "must capture the Active Bank as it was BEFORE this install (BANK_2/1u here), to detect a real switch later");
}

void test_process_validating_moves_to_installing_and_issues_reset_request(void) {
    make_ready();
    FirmwareUpdate_SubmitInstallRequest();

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_INSTALLING, FirmwareUpdate_GetState());
    TEST_ASSERT_EQUAL_INT(1, s_reset_request_call_count);
    TEST_ASSERT_EQUAL_INT(RESET_TYPE_SYSTEM_RESET, s_last_reset_request.type);
    TEST_ASSERT_EQUAL_STRING("MCU", s_last_reset_request.target);
    TEST_ASSERT_EQUAL_INT(RESET_SOURCE_FIRMWARE_UPDATE, s_last_reset_request.source);
    TEST_ASSERT_EQUAL_INT(RESET_REASON_FIRMWARE_INSTALL_REQUESTED, s_last_reset_request.reason);
}

void test_process_validating_fails_when_storage_recovery_read_fails(void) {
    make_ready();
    FirmwareUpdate_SubmitInstallRequest();
    s_stub_storage_read_result = STORAGE_ERROR; /* today's reality - STORAGE_RECOVERY has no real backend yet */

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState(),
        "cannot safely guess active_bank - must not write or request a Reset");
    TEST_ASSERT_EQUAL_INT(0, s_storage_write_call_count);
    TEST_ASSERT_EQUAL_INT(0, s_reset_request_call_count);
}

void test_process_validating_fails_when_storage_recovery_write_fails(void) {
    make_ready();
    FirmwareUpdate_SubmitInstallRequest();
    s_stub_storage_write_result = STORAGE_ERROR;

    FirmwareUpdate_Process();

    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_FAILED, FirmwareUpdate_GetState());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_call_count,
        "a Reset must never be requested unless Boot Action was actually written");
}

void test_installing_is_a_dead_end_a_second_install_request_is_rejected(void) {
    make_ready();
    FirmwareUpdate_SubmitInstallRequest();
    FirmwareUpdate_Process(); /* -> INSTALLING */

    TEST_ASSERT_FALSE(FirmwareUpdate_SubmitInstallRequest());
    TEST_ASSERT_EQUAL_INT(FIRMWARE_UPDATE_INSTALLING, FirmwareUpdate_GetState());
}

/* --- EVENT_FIRMWARE_UPDATE_COMPLETED (explicit user decision: "این یک
 * ایونته... هیچ فرقی با شاک سنسور یا ایونت روشن شدن نداره. هروقت فعال بشه
 * باید به سرور برسه" - see firmware_update_management.h's "SCOPE, PART 3"
 * and firmware_update_management.c's FirmwareUpdatePendingInstallRecord/
 * check_install_completed() for the exact mechanism). Each test here
 * re-calls FirmwareUpdate_Init() itself, after overriding the on-disk
 * fixtures, to simulate "the device just booted" - setUp()'s own
 * FirmwareUpdate_Init() call already ran once with the default (no install
 * pending) fixture, exactly like every other test in this file. --- */

void test_init_pushes_completed_event_when_active_bank_actually_changed(void) {
    s_stub_pending_install_on_disk.pending               = 1u;
    s_stub_pending_install_on_disk.previous_active_bank  = 0u; /* BANK_1 was active before the install Reset */
    s_stub_boot_control_on_disk.active_bank              = 1u; /* now BANK_2 - Bootloader actually switched */

    FirmwareUpdate_Init();

    Event event;
    TEST_ASSERT_TRUE_MESSAGE(pop_event(&event), "a genuine Bank switch must push an Event, exactly like any other Detection->Event producer (Shock, ACC)");
    TEST_ASSERT_EQUAL_INT(EVENT_FIRMWARE_UPDATE_COMPLETED, event.event_type);
}

void test_init_does_not_push_completed_event_when_active_bank_unchanged(void) {
    /* Bootloader fell back to the previous Bank (CRC failure / retries
     * exhausted, bootloader.md section 9) - the install did NOT complete. */
    s_stub_pending_install_on_disk.pending               = 1u;
    s_stub_pending_install_on_disk.previous_active_bank  = 0u;
    s_stub_boot_control_on_disk.active_bank              = 0u; /* unchanged */

    FirmwareUpdate_Init();

    Event event;
    TEST_ASSERT_FALSE_MESSAGE(pop_event(&event), "no real install completed - nothing to report");
}

void test_init_does_not_push_completed_event_when_no_install_was_pending(void) {
    /* setUp() already called FirmwareUpdate_Init() once with the default
     * (pending=0) fixture, simulating an ordinary boot - just confirm
     * nothing was pushed. */
    Event event;
    TEST_ASSERT_FALSE(pop_event(&event));
}

void test_init_clears_the_pending_flag_after_checking(void) {
    s_stub_pending_install_on_disk.pending               = 1u;
    s_stub_pending_install_on_disk.previous_active_bank  = 0u;
    s_stub_boot_control_on_disk.active_bank              = 1u;

    FirmwareUpdate_Init();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, s_last_written_pending_install.pending,
        "the flag must be consumed so a later, unrelated boot never re-checks it");
}

void test_init_is_a_safe_noop_when_storage_recovery_has_no_backend(void) {
    /* Today's reality - STORAGE_RECOVERY has no real backend yet, same
     * "logic real, hardware pending" shape as everywhere else in this
     * file. */
    s_stub_storage_read_result = STORAGE_ERROR;

    FirmwareUpdate_Init();

    Event event;
    TEST_ASSERT_FALSE(pop_event(&event));
    TEST_ASSERT_EQUAL_INT(0, s_storage_write_call_count);
}
