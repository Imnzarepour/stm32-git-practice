#include "unity.h"
#include "infrastructure/configuration.h"
#include "Mockstorage.h"
#include <string.h>
#include <stddef.h>

/* Configuration Infrastructure's only real dependency is Storage
 * Infrastructure (STORAGE_CONFIG) - Mocked here via CMock, same pattern
 * every other Management test uses for its own direct dependencies.
 *
 * Storage_Read/Storage_Write are wired via *_StubWithCallback (not
 * *_ExpectAndReturn) because they carry an opaque void* buffer that CMock
 * cannot compare/fill by value without extra plugin configuration - the
 * stub callbacks below do the memcpy by hand instead, the same technique
 * test_call_management.c uses for EC200U_Call_GetIncomingNumber(). */

static uint8_t       s_persisted_bytes[STORAGE_CONFIG_CAPACITY_BYTES];
static StorageResult s_stub_read_result;
static StorageResult s_stub_write_result;

static uint32_t s_write_call_count;
static StorageObject s_last_write_object;
static uint32_t      s_last_write_offset;
static uint32_t      s_last_write_length;

static StorageResult stub_storage_read(StorageObject object, uint32_t offset, void* buffer, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    TEST_ASSERT_EQUAL_INT(STORAGE_CONFIG, object);
    if (s_stub_read_result == STORAGE_OK) {
        memcpy(buffer, &s_persisted_bytes[offset], length);
    }
    return s_stub_read_result;
}

static StorageResult stub_storage_write(StorageObject object, uint32_t offset, const void* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_write_call_count++;
    s_last_write_object = object;
    s_last_write_offset = offset;
    s_last_write_length = length;
    if (s_stub_write_result == STORAGE_OK) {
        memcpy(&s_persisted_bytes[offset], data, length);
    }
    return s_stub_write_result;
}

void setUp(void) {
    memset(s_persisted_bytes, 0, sizeof(s_persisted_bytes));
    s_stub_read_result  = STORAGE_ERROR; /* default: nothing valid persisted yet */
    s_stub_write_result = STORAGE_OK;

    s_write_call_count = 0;
    s_last_write_object = STORAGE_EVENT; /* deliberately not STORAGE_CONFIG, so an untouched value is obvious */
    s_last_write_offset = 0;
    s_last_write_length = 0;

    Storage_Read_StubWithCallback(stub_storage_read);
    Storage_Write_StubWithCallback(stub_storage_write);

    Configuration_Init();
}

void tearDown(void) {}

void test_init_falls_back_to_factory_defaults_when_storage_read_fails(void) {
    const DeviceConfig* config = Configuration_Get();

    /* Non-empty, explicit user decision - see LoadFactoryDefaults()
     * (infrastructure/configuration.c). Every PLACEHOLDER value there is
     * asserted here so a future edit to that function gets a failing
     * test as a reminder to update this one too. */
    TEST_ASSERT_EQUAL_STRING("UNSET-DEVICE-ID", config->identity.device_id);
    TEST_ASSERT_EQUAL_STRING("UNSET-SN", config->production.serial_number);
    TEST_ASSERT_EQUAL_STRING("UNSET", config->production.hardware_version);
    TEST_ASSERT_EQUAL_STRING("UNSET-BATCH", config->production.batch_info);
    TEST_ASSERT_EQUAL_STRING("sandbox.example.com", config->communication.server_address_sandbox);
    TEST_ASSERT_EQUAL_STRING("deployment.example.com", config->communication.server_address_deployment);
    TEST_ASSERT_EQUAL_STRING("operation.example.com", config->communication.server_address_operation);
    TEST_ASSERT_EQUAL_MESSAGE(CONFIG_SERVER_ENV_SANDBOX, config->communication.active_environment,
        "Sandbox must be the factory-default active environment");

    TEST_ASSERT_EQUAL_UINT32(1, config->access.allowed_phone_numbers.count);
    TEST_ASSERT_EQUAL_STRING("+980000000099", config->access.allowed_phone_numbers.numbers[0].number);
}

void test_init_loads_persisted_values_when_storage_read_succeeds(void) {
    DeviceConfig persisted;
    memset(&persisted, 0, sizeof(persisted));
    strncpy(persisted.identity.device_id, "TCU-0001", CONFIG_DEVICE_ID_MAX_LENGTH - 1);
    memcpy(s_persisted_bytes, &persisted, sizeof(persisted));
    s_stub_read_result = STORAGE_OK;

    Configuration_Init();

    TEST_ASSERT_EQUAL_STRING("TCU-0001", Configuration_Get()->identity.device_id);
}

void test_get_returns_a_stable_pointer_with_no_storage_access(void) {
    const DeviceConfig* first  = Configuration_Get();
    const DeviceConfig* second = Configuration_Get();

    TEST_ASSERT_EQUAL_PTR(first, second);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_write_call_count, "Configuration_Get() must never write to Storage");
}

void test_update_writes_through_and_updates_runtime(void) {
    char new_id[CONFIG_DEVICE_ID_MAX_LENGTH] = "TCU-9999";

    ConfigResult result = Configuration_Update(CONFIG_FIELD_DEVICE_ID, new_id, sizeof(new_id));

    TEST_ASSERT_EQUAL_INT(CONFIG_OK, result);
    TEST_ASSERT_EQUAL_STRING("TCU-9999", Configuration_Get()->identity.device_id);
    TEST_ASSERT_EQUAL_UINT32(1, s_write_call_count);
    TEST_ASSERT_EQUAL_INT(STORAGE_CONFIG, s_last_write_object);
    TEST_ASSERT_EQUAL_UINT32(sizeof(new_id), s_last_write_length);
}

void test_update_allowed_phone_numbers_round_trip(void) {
    ConfigAllowedPhoneNumbers numbers;
    memset(&numbers, 0, sizeof(numbers));
    numbers.count = 2;
    strncpy(numbers.numbers[0].number, "+980000000001", CONFIG_PHONE_NUMBER_MAX_LENGTH - 1);
    strncpy(numbers.numbers[1].number, "+980000000002", CONFIG_PHONE_NUMBER_MAX_LENGTH - 1);

    ConfigResult result = Configuration_Update(CONFIG_FIELD_ALLOWED_PHONE_NUMBERS, &numbers, sizeof(numbers));

    TEST_ASSERT_EQUAL_INT(CONFIG_OK, result);
    const ConfigAllowedPhoneNumbers* stored = &Configuration_Get()->access.allowed_phone_numbers;
    TEST_ASSERT_EQUAL_UINT32(2, stored->count);
    TEST_ASSERT_EQUAL_STRING("+980000000001", stored->numbers[0].number);
    TEST_ASSERT_EQUAL_STRING("+980000000002", stored->numbers[1].number);
}

void test_update_rejects_length_mismatch_and_does_not_touch_storage(void) {
    char new_id[CONFIG_DEVICE_ID_MAX_LENGTH] = "TCU-9999";

    ConfigResult result = Configuration_Update(CONFIG_FIELD_DEVICE_ID, new_id, sizeof(new_id) - 1);

    TEST_ASSERT_EQUAL_INT(CONFIG_ERROR, result);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_write_call_count, "A size mismatch must be rejected before ever calling Storage");
    TEST_ASSERT_EQUAL_STRING("UNSET-DEVICE-ID", Configuration_Get()->identity.device_id);
}

void test_update_rejects_invalid_field(void) {
    char value[4] = { 0 };

    ConfigResult result = Configuration_Update(CONFIG_FIELD_COUNT, value, sizeof(value));

    TEST_ASSERT_EQUAL_INT(CONFIG_ERROR, result);
    TEST_ASSERT_EQUAL_UINT32(0, s_write_call_count);
}

void test_update_rejects_null_value(void) {
    ConfigResult result = Configuration_Update(CONFIG_FIELD_DEVICE_ID, NULL, CONFIG_DEVICE_ID_MAX_LENGTH);

    TEST_ASSERT_EQUAL_INT(CONFIG_ERROR, result);
    TEST_ASSERT_EQUAL_UINT32(0, s_write_call_count);
}

void test_update_keeps_runtime_unchanged_when_storage_write_fails(void) {
    char first_id[CONFIG_DEVICE_ID_MAX_LENGTH] = "TCU-0001";
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_Update(CONFIG_FIELD_DEVICE_ID, first_id, sizeof(first_id)));

    s_stub_write_result = STORAGE_ERROR;
    char attempted_id[CONFIG_DEVICE_ID_MAX_LENGTH] = "TCU-HACK";
    ConfigResult result = Configuration_Update(CONFIG_FIELD_DEVICE_ID, attempted_id, sizeof(attempted_id));

    TEST_ASSERT_EQUAL_INT(CONFIG_ERROR, result);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("TCU-0001", Configuration_Get()->identity.device_id,
        "Runtime must keep the old value when the persistent write fails - never a RAM/persistent split");
}

void test_update_returns_busy_and_keeps_runtime_unchanged_when_storage_is_busy(void) {
    s_stub_write_result = STORAGE_BUSY;
    char new_id[CONFIG_DEVICE_ID_MAX_LENGTH] = "TCU-9999";

    ConfigResult result = Configuration_Update(CONFIG_FIELD_DEVICE_ID, new_id, sizeof(new_id));

    TEST_ASSERT_EQUAL_INT(CONFIG_BUSY, result);
    TEST_ASSERT_EQUAL_STRING("UNSET-DEVICE-ID", Configuration_Get()->identity.device_id);
}

/* --- Small single-field setters ---
 * Each one happy-path test proves that setter is wired to the correct
 * Field (right offset/length reaching Storage, right struct member
 * updated) - the underlying write-then-update-runtime behavior itself is
 * already fully covered above via Configuration_Update() directly, so it
 * is not re-proven per setter. Truncation/NULL-rejection are each proven
 * once, since every string setter shares the exact same SetFixedString()
 * body (configuration.c). */

void test_set_device_id_updates_runtime_and_storage(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_SetDeviceId("TCU-0042"));

    TEST_ASSERT_EQUAL_STRING("TCU-0042", Configuration_Get()->identity.device_id);
    TEST_ASSERT_EQUAL_UINT32(1, s_write_call_count);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, identity.device_id), s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_DEVICE_ID_MAX_LENGTH, s_last_write_length);
}

void test_set_serial_number_updates_runtime_and_storage(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_SetSerialNumber("SN-12345"));

    TEST_ASSERT_EQUAL_STRING("SN-12345", Configuration_Get()->production.serial_number);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, production.serial_number), s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_SERIAL_NUMBER_MAX_LENGTH, s_last_write_length);
}

void test_set_hardware_version_updates_runtime_and_storage(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_SetHardwareVersion("F4-R1"));

    TEST_ASSERT_EQUAL_STRING("F4-R1", Configuration_Get()->production.hardware_version);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, production.hardware_version), s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_HARDWARE_VERSION_MAX_LENGTH, s_last_write_length);
}

void test_set_batch_info_updates_runtime_and_storage(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_SetBatchInfo("BATCH-2026-03"));

    TEST_ASSERT_EQUAL_STRING("BATCH-2026-03", Configuration_Get()->production.batch_info);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, production.batch_info), s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_BATCH_INFO_MAX_LENGTH, s_last_write_length);
}

void test_set_server_address_sandbox_updates_runtime_and_storage(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_SetServerAddressSandbox("sandbox.real-server.local"));

    TEST_ASSERT_EQUAL_STRING("sandbox.real-server.local", Configuration_Get()->communication.server_address_sandbox);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, communication.server_address_sandbox), s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_SERVER_ADDRESS_MAX_LENGTH, s_last_write_length);
}

void test_set_server_address_deployment_updates_runtime_and_storage(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_SetServerAddressDeployment("deploy.real-server.local"));

    TEST_ASSERT_EQUAL_STRING("deploy.real-server.local", Configuration_Get()->communication.server_address_deployment);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, communication.server_address_deployment), s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_SERVER_ADDRESS_MAX_LENGTH, s_last_write_length);
}

void test_set_server_address_operation_updates_runtime_and_storage(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_SetServerAddressOperation("ops.real-server.local"));

    TEST_ASSERT_EQUAL_STRING("ops.real-server.local", Configuration_Get()->communication.server_address_operation);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, communication.server_address_operation), s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_SERVER_ADDRESS_MAX_LENGTH, s_last_write_length);
}

void test_set_active_environment_updates_runtime_and_storage(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_OK, Configuration_SetActiveServerEnvironment(CONFIG_SERVER_ENV_OPERATION));

    TEST_ASSERT_EQUAL(CONFIG_SERVER_ENV_OPERATION, Configuration_Get()->communication.active_environment);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, communication.active_environment), s_last_write_offset);
    TEST_ASSERT_EQUAL_UINT32(sizeof(ConfigServerEnvironment), s_last_write_length);
}

void test_set_active_environment_rejects_out_of_range_value(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_ERROR, Configuration_SetActiveServerEnvironment((ConfigServerEnvironment)99));
    TEST_ASSERT_EQUAL_MESSAGE(CONFIG_SERVER_ENV_SANDBOX, Configuration_Get()->communication.active_environment,
        "Runtime must stay at its previous value, not the rejected one");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_write_call_count, "an invalid environment must never reach Storage");
}

void test_string_setter_rejects_null(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_ERROR, Configuration_SetSerialNumber(NULL));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_write_call_count, "A NULL value must be rejected before ever calling Storage");
}

void test_string_setter_truncates_an_oversized_value_without_overflow(void) {
    /* CONFIG_SERIAL_NUMBER_MAX_LENGTH is 16 (15 usable chars + NUL) -
     * this is deliberately longer than that. */
    ConfigResult result = Configuration_SetSerialNumber("SN-WAY-TOO-LONG-TO-EVER-FIT");

    TEST_ASSERT_EQUAL_INT(CONFIG_OK, result);
    const char* stored = Configuration_Get()->production.serial_number;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(CONFIG_SERIAL_NUMBER_MAX_LENGTH - 1, strlen(stored),
        "Must be truncated to exactly (buffer size - 1), never overflowed and never left unterminated");
    TEST_ASSERT_EQUAL_STRING("SN-WAY-TOO-LONG", stored);
}

/* --- Configuration_SetAllowedPhoneNumbers() --- */

void test_set_allowed_phone_numbers_updates_runtime_and_storage(void) {
    const char* numbers[] = { "+980000000001", "+980000000002" };

    ConfigResult result = Configuration_SetAllowedPhoneNumbers(numbers, 2);

    TEST_ASSERT_EQUAL_INT(CONFIG_OK, result);
    const ConfigAllowedPhoneNumbers* stored = &Configuration_Get()->access.allowed_phone_numbers;
    TEST_ASSERT_EQUAL_UINT32(2, stored->count);
    TEST_ASSERT_EQUAL_STRING("+980000000001", stored->numbers[0].number);
    TEST_ASSERT_EQUAL_STRING("+980000000002", stored->numbers[1].number);
    TEST_ASSERT_EQUAL_UINT32(offsetof(DeviceConfig, access.allowed_phone_numbers), s_last_write_offset);
}

void test_set_allowed_phone_numbers_rejects_null_array(void) {
    TEST_ASSERT_EQUAL_INT(CONFIG_ERROR, Configuration_SetAllowedPhoneNumbers(NULL, 3));
    TEST_ASSERT_EQUAL_UINT32(0, s_write_call_count);
}

void test_set_allowed_phone_numbers_caps_count_at_the_maximum(void) {
    const char* numbers[CONFIG_MAX_ALLOWED_PHONE_NUMBERS + 2] = {
        "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10"
    };

    ConfigResult result = Configuration_SetAllowedPhoneNumbers(numbers, CONFIG_MAX_ALLOWED_PHONE_NUMBERS + 2);

    TEST_ASSERT_EQUAL_INT(CONFIG_OK, result);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(CONFIG_MAX_ALLOWED_PHONE_NUMBERS,
        Configuration_Get()->access.allowed_phone_numbers.count,
        "A count above the maximum must be capped, not overflow the numbers[] array");
}

void test_set_allowed_phone_numbers_stops_at_a_null_entry(void) {
    const char* numbers[] = { "+980000000001", NULL, "+980000000003" };

    ConfigResult result = Configuration_SetAllowedPhoneNumbers(numbers, 3);

    TEST_ASSERT_EQUAL_INT(CONFIG_OK, result);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, Configuration_Get()->access.allowed_phone_numbers.count,
        "Must stop at the NULL entry, never read/trust array slots past it");
}

void test_set_allowed_phone_numbers_truncates_an_oversized_number(void) {
    const char* numbers[] = { "+98000000000000000000000000TOO-LONG" };

    ConfigResult result = Configuration_SetAllowedPhoneNumbers(numbers, 1);

    TEST_ASSERT_EQUAL_INT(CONFIG_OK, result);
    TEST_ASSERT_EQUAL_UINT32(CONFIG_PHONE_NUMBER_MAX_LENGTH - 1,
        strlen(Configuration_Get()->access.allowed_phone_numbers.numbers[0].number));
}
