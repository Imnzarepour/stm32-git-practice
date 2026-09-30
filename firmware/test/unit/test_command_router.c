#include "unity.h"
#include "shared/command_router.h"
#include "infrastructure/time_base.h"
#include "Mockdiagnostic_management.h"
#include "Mockfirmware_update_management.h"
#include "Mockactuator_management.h"
#include <string.h>

TEST_SOURCE_FILE("shared/command_safety_policy.c")
TEST_SOURCE_FILE("infrastructure/time_base.c")

/* CommandRouter_Submit()'s real dependencies are Diagnostic Management's,
 * Firmware Update Management's and Actuator Management's own Request
 * APIs - all Mocked here so this test isolates the router's own
 * domain->owner mapping and malformed/busy handling from each owner's
 * actual state machine (same reasoning test_event_policy.c documents for
 * its own SMS/Call dependencies). Time Base is real (needed only for
 * route_to_actuator_door_gpio()'s Time_GetTick() call).
 * CommandSafetyPolicy_IsAllowed() is real (linked above) since it is a
 * pure, deterministic function and exactly what this router is supposed
 * to enforce, not something to fake away. */

static int                    s_submit_call_count;
static DiagnosticRequest      s_last_request;
static DiagnosticSubmitResult s_stub_result;

static DiagnosticSubmitResult stub_submit(const DiagnosticRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_submit_call_count++;
    if (request) s_last_request = *request;
    return s_stub_result;
}

static int      s_firmware_submit_call_count;
static uint32_t s_last_firmware_expected_size;
static char     s_last_firmware_url[FIRMWARE_DOWNLOAD_URL_MAX_LENGTH];
static bool     s_stub_firmware_result;

static bool stub_firmware_submit(uint32_t expected_size, const char* url, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_firmware_submit_call_count++;
    s_last_firmware_expected_size = expected_size;
    if (url) strncpy(s_last_firmware_url, url, sizeof(s_last_firmware_url) - 1u);
    return s_stub_firmware_result;
}

static int  s_firmware_install_call_count;
static bool s_stub_firmware_install_result;

static bool stub_firmware_install(int cmock_num_calls) {
    (void)cmock_num_calls;
    s_firmware_install_call_count++;
    return s_stub_firmware_install_result;
}

static int             s_actuator_submit_call_count;
static ActuatorRequest s_last_actuator_request;
static bool             s_stub_actuator_result;

static bool stub_actuator_submit(const ActuatorRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_actuator_submit_call_count++;
    if (request) s_last_actuator_request = *request;
    return s_stub_actuator_result;
}

void setUp(void) {
    Time_Init();

    s_submit_call_count = 0;
    memset(&s_last_request, 0, sizeof(s_last_request));
    s_stub_result = DIAG_SUBMIT_ACCEPTED;
    Diagnostic_SubmitRequest_StubWithCallback(stub_submit);

    s_firmware_submit_call_count  = 0;
    s_last_firmware_expected_size = 0;
    memset(s_last_firmware_url, 0, sizeof(s_last_firmware_url));
    s_stub_firmware_result        = true;
    FirmwareUpdate_SubmitRequest_StubWithCallback(stub_firmware_submit);

    s_firmware_install_call_count  = 0;
    s_stub_firmware_install_result = true;
    FirmwareUpdate_SubmitInstallRequest_StubWithCallback(stub_firmware_install);

    s_actuator_submit_call_count = 0;
    memset(&s_last_actuator_request, 0, sizeof(s_last_actuator_request));
    s_stub_actuator_result = true;
    Actuator_SubmitRequest_StubWithCallback(stub_actuator_submit);
}

void tearDown(void) {}

static Command make_diagnostic_command(uint32_t operation, uint32_t target, uint32_t parameter) {
    Command command;
    command.domain    = COMMAND_DOMAIN_DIAGNOSTIC;
    command.operation = operation;
    command.target     = target;
    command.parameter  = parameter;
    return command;
}

void test_null_command_is_rejected_as_malformed(void) {
    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_REJECTED_MALFORMED, CommandRouter_Submit(NULL));
    TEST_ASSERT_EQUAL_INT(0, s_submit_call_count);
}

void test_diagnostic_clear_dtc_is_accepted_and_forwarded_unchanged(void) {
    Command command = make_diagnostic_command(2u /* DIAG_OP_CLEAR_DTC */, 7u, 0u);

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(1, s_submit_call_count);
    TEST_ASSERT_EQUAL_INT(DIAG_OP_CLEAR_DTC, s_last_request.operation);
    TEST_ASSERT_EQUAL_UINT32(7u, s_last_request.target);
    TEST_ASSERT_EQUAL_UINT32(0u, s_last_request.parameter);
}

void test_diagnostic_read_parameter_forwards_its_parameter_field(void) {
    Command command = make_diagnostic_command(0u /* DIAG_OP_READ_PARAMETER */, 3u, 42u);

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(DIAG_OP_READ_PARAMETER, s_last_request.operation);
    TEST_ASSERT_EQUAL_UINT32(3u, s_last_request.target);
    TEST_ASSERT_EQUAL_UINT32(42u, s_last_request.parameter);
}

void test_diagnostic_pending_still_counts_as_accepted(void) {
    Command command = make_diagnostic_command(1u, 1u, 0u);
    s_stub_result = DIAG_SUBMIT_PENDING;

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_Submit(&command));
}

void test_diagnostic_busy_is_reported_as_busy(void) {
    Command command = make_diagnostic_command(1u, 1u, 0u);
    s_stub_result = DIAG_SUBMIT_BUSY;

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_BUSY, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(1, s_submit_call_count);
}

void test_out_of_range_diagnostic_operation_is_rejected_as_malformed(void) {
    Command command = make_diagnostic_command(99u, 1u, 0u); /* not a known DiagnosticOperation */

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_REJECTED_MALFORMED, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_submit_call_count,
        "an unrecognized operation must never reach Diagnostic_SubmitRequest()");
}

/* COMMAND_DOMAIN_FIRMWARE_DOWNLOAD no longer goes through Command/
 * CommandRouter_Submit() at all (it carries a URL, which Command has no
 * room for - see command_router.h's comment on FirmwareDownloadRequest) -
 * CommandRouter_SubmitFirmwareDownload() is its only entry point now. */
void test_firmware_download_is_accepted_and_forwards_size_and_url(void) {
    FirmwareDownloadRequest request;
    request.expected_size = 262144u;
    strncpy(request.url, "https://example.com/fw.bin", sizeof(request.url) - 1u);

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_SubmitFirmwareDownload(&request));
    TEST_ASSERT_EQUAL_INT(1, s_firmware_submit_call_count);
    TEST_ASSERT_EQUAL_UINT32(262144u, s_last_firmware_expected_size);
    TEST_ASSERT_EQUAL_STRING("https://example.com/fw.bin", s_last_firmware_url);
    TEST_ASSERT_EQUAL_INT(0, s_submit_call_count); /* must never also reach Diagnostic */
}

void test_firmware_download_rejected_by_owner_is_reported_as_busy(void) {
    FirmwareDownloadRequest request;
    request.expected_size = 1024u;
    strncpy(request.url, "https://example.com/fw.bin", sizeof(request.url) - 1u);
    s_stub_firmware_result = false; /* already downloading, expected_size was 0, or url was empty */

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_BUSY, CommandRouter_SubmitFirmwareDownload(&request));
    TEST_ASSERT_EQUAL_INT(1, s_firmware_submit_call_count);
}

void test_firmware_download_null_request_is_rejected_as_malformed(void) {
    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_REJECTED_MALFORMED, CommandRouter_SubmitFirmwareDownload(NULL));
    TEST_ASSERT_EQUAL_INT(0, s_firmware_submit_call_count);
}

void test_firmware_download_domain_via_generic_submit_is_rejected_as_malformed(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_FIRMWARE_DOWNLOAD;
    command.operation = 0u;
    command.target     = 0u;
    command.parameter  = 262144u;

    TEST_ASSERT_EQUAL_INT_MESSAGE(COMMAND_ROUTER_REJECTED_MALFORMED, CommandRouter_Submit(&command),
        "this domain must arrive via CommandRouter_SubmitFirmwareDownload() only, never the generic Command path");
    TEST_ASSERT_EQUAL_INT(0, s_firmware_submit_call_count);
}

void test_unknown_domain_is_rejected_by_policy_before_reaching_any_owner(void) {
    Command command;
    command.domain    = (CommandDomain)99; /* not COMMAND_DOMAIN_DIAGNOSTIC - fail-closed */
    command.operation = 0u;
    command.target     = 0u;
    command.parameter  = 0u;

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_REJECTED_POLICY, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(0, s_submit_call_count);
}

/* --- COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST (Door Lock/Unlock via
 * Diagnostic - explicit user decision, OPEN-ITEMS.md 🔴 critical item) --- */

void test_actuator_test_is_accepted_and_forwards_target_and_parameter(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST;
    command.operation = 0xDEADBEEFu; /* unused for this domain - must not leak into the DiagnosticRequest */
    command.target     = 0x100u;
    command.parameter  = 1u; /* open/unlock */

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(1, s_submit_call_count);
    TEST_ASSERT_EQUAL_INT(DIAG_OP_ACTUATOR_TEST, s_last_request.operation);
    TEST_ASSERT_EQUAL_UINT32(0x100u, s_last_request.target);
    TEST_ASSERT_EQUAL_UINT32(1u, s_last_request.parameter);
}

void test_actuator_test_pending_still_counts_as_accepted(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST;
    command.operation = 0u;
    command.target     = 0x100u;
    command.parameter  = 0u; /* close/lock */
    s_stub_result = DIAG_SUBMIT_PENDING;

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_Submit(&command));
}

void test_actuator_test_busy_is_reported_as_busy(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST;
    command.operation = 0u;
    command.target     = 0x100u;
    command.parameter  = 0u;
    s_stub_result = DIAG_SUBMIT_BUSY;

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_BUSY, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(1, s_submit_call_count);
}

/* --- COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO (Door Lock/Unlock via direct GPIO
 * relay - second, independent mechanism alongside Diagnostic Actuator
 * Test above, explicit user decision, OPEN-ITEMS.md 🔴 critical item) --- */

void test_door_gpio_open_commands_actuator_door_unlock(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO;
    command.operation = 0xDEADBEEFu; /* unused for this domain */
    command.target     = 0xCAFEu;     /* unused for this domain - Actuator Management addresses by ActuatorId, not an ECU id */
    command.parameter  = 1u; /* open/unlock */

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(1, s_actuator_submit_call_count);
    TEST_ASSERT_EQUAL_INT(ACTUATOR_DOOR_UNLOCK, s_last_actuator_request.actuator);
    TEST_ASSERT_EQUAL_INT(ACTUATOR_CMD_ACTIVATE, s_last_actuator_request.command);
}

void test_door_gpio_close_commands_actuator_door_lock(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO;
    command.operation = 0u;
    command.target     = 0u;
    command.parameter  = 0u; /* close/lock */

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(ACTUATOR_DOOR_LOCK, s_last_actuator_request.actuator);
    TEST_ASSERT_EQUAL_INT(ACTUATOR_CMD_ACTIVATE, s_last_actuator_request.command);
}

void test_door_gpio_never_reaches_diagnostic_or_firmware_update(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO;
    command.operation = 0u;
    command.target     = 0u;
    command.parameter  = 1u;

    CommandRouter_Submit(&command);

    TEST_ASSERT_EQUAL_INT(0, s_submit_call_count);
    TEST_ASSERT_EQUAL_INT(0, s_firmware_submit_call_count);
}

/* --- COMMAND_DOMAIN_FIRMWARE_INSTALL (Server -> enter Boot / install the
 * already-downloaded image - explicit user decision, given by direct
 * analogy to the Door Lock/Unlock domains above) --- */

void test_firmware_install_is_accepted_when_owner_accepts(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_FIRMWARE_INSTALL;
    command.operation = 0xDEADBEEFu; /* unused for this domain */
    command.target     = 0xCAFEu;     /* unused for this domain */
    command.parameter  = 0u;          /* unused for this domain */

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_ACCEPTED, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(1, s_firmware_install_call_count);
}

void test_firmware_install_rejected_by_owner_is_reported_as_busy(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_FIRMWARE_INSTALL;
    command.operation = 0u;
    command.target     = 0u;
    command.parameter  = 0u;
    s_stub_firmware_install_result = false; /* not currently READY */

    TEST_ASSERT_EQUAL_INT(COMMAND_ROUTER_BUSY, CommandRouter_Submit(&command));
    TEST_ASSERT_EQUAL_INT(1, s_firmware_install_call_count);
}

void test_firmware_install_never_reaches_diagnostic_or_firmware_download(void) {
    Command command;
    command.domain    = COMMAND_DOMAIN_FIRMWARE_INSTALL;
    command.operation = 0u;
    command.target     = 0u;
    command.parameter  = 0u;

    CommandRouter_Submit(&command);

    TEST_ASSERT_EQUAL_INT(0, s_submit_call_count);
    TEST_ASSERT_EQUAL_INT(0, s_firmware_submit_call_count);
}
