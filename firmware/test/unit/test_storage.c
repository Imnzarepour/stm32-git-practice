#include "unity.h"
#include "infrastructure/storage.h"
#include "Mockhardware_proxy_storage.h"

/* Storage Infrastructure's only dependency is the Hardware Proxy Storage
 * domain - Mocked here via CMock (never the real driver/EEPROM-emulation/
 * file-backed-simulation code, per hardware-proxy.md section 6).
 * STORAGE_CONFIG, STORAGE_RECOVERY and STORAGE_FIRMWARE_UPDATE all map
 * onto their own Region and reach the proxy; STORAGE_EVENT/
 * STORAGE_TELEMETRY/STORAGE_LOG are still unimplemented placeholders that
 * never reach it at all, which the tests below prove directly. */

void setUp(void) {}
void tearDown(void) {}

void test_read_maps_storage_config_to_the_config_region(void) {
    uint8_t buffer[4];

    HWProxy_Storage_Read_ExpectAndReturn(HWPROXY_STORAGE_REGION_CONFIG, 10, buffer, sizeof(buffer), HWPROXY_STORAGE_OK);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, Storage_Read(STORAGE_CONFIG, 10, buffer, sizeof(buffer)));
}

void test_write_maps_storage_config_to_the_config_region(void) {
    uint8_t data[4] = { 1, 2, 3, 4 };

    HWProxy_Storage_Write_ExpectAndReturn(HWPROXY_STORAGE_REGION_CONFIG, 20, data, sizeof(data), HWPROXY_STORAGE_OK);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, Storage_Write(STORAGE_CONFIG, 20, data, sizeof(data)));
}

void test_erase_maps_storage_config_to_the_config_region(void) {
    HWProxy_Storage_Erase_ExpectAndReturn(HWPROXY_STORAGE_REGION_CONFIG, HWPROXY_STORAGE_OK);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, Storage_Erase(STORAGE_CONFIG));
}

void test_get_status_reports_ok_when_the_proxy_is_ready(void) {
    HWProxy_Storage_Init_ExpectAndReturn(HWPROXY_STORAGE_OK);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, Storage_GetStatus(STORAGE_CONFIG));
}

void test_proxy_error_is_translated_to_storage_error(void) {
    uint8_t buffer[4];
    HWProxy_Storage_Read_ExpectAndReturn(HWPROXY_STORAGE_REGION_CONFIG, 0, buffer, sizeof(buffer), HWPROXY_STORAGE_ERROR);

    TEST_ASSERT_EQUAL_INT(STORAGE_ERROR, Storage_Read(STORAGE_CONFIG, 0, buffer, sizeof(buffer)));
}

void test_read_rejects_a_range_past_the_reserved_capacity_without_calling_the_proxy(void) {
    uint8_t buffer[4];

    TEST_ASSERT_EQUAL_INT(STORAGE_INVALID, Storage_Read(STORAGE_CONFIG, STORAGE_CONFIG_CAPACITY_BYTES - 1, buffer, sizeof(buffer)));
}

void test_read_rejects_a_null_buffer_without_calling_the_proxy(void) {
    TEST_ASSERT_EQUAL_INT(STORAGE_INVALID, Storage_Read(STORAGE_CONFIG, 0, NULL, 4));
}

void test_objects_with_no_backend_decision_yet_are_still_unimplemented_placeholders(void) {
    /* No mock expectation set on any HWProxy_Storage_* call - CMock fails
     * the test if one is invoked, proving these objects never reach the
     * proxy (no backend decision exists at all yet for any of them -
     * OPEN-ITEMS.md). STORAGE_RECOVERY/STORAGE_FIRMWARE_UPDATE moved out of
     * this list - see the dedicated tests below for their own Region
     * mapping. */
    uint8_t buffer[4];

    TEST_ASSERT_EQUAL_INT(STORAGE_ERROR, Storage_Read(STORAGE_EVENT, 0, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERROR, Storage_Erase(STORAGE_TELEMETRY));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERROR, Storage_Write(STORAGE_LOG, 0, buffer, sizeof(buffer)));
}

/* --- STORAGE_RECOVERY / STORAGE_FIRMWARE_UPDATE: Host-Simulation Regions
 * (see hardware_proxy_storage.h/.c's own comments) - same dispatch shape
 * as STORAGE_CONFIG above, just a different Region enum value. Neither
 * object's real physical backend is decided (OPEN-ITEMS.md); this only
 * proves Storage Infrastructure's own mapping/Result-translation is
 * correct, same as it already does for STORAGE_CONFIG. --- */

void test_read_maps_storage_recovery_to_the_recovery_region(void) {
    uint8_t buffer[4];

    HWProxy_Storage_Read_ExpectAndReturn(HWPROXY_STORAGE_REGION_RECOVERY, 8, buffer, sizeof(buffer), HWPROXY_STORAGE_OK);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, Storage_Read(STORAGE_RECOVERY, 8, buffer, sizeof(buffer)));
}

void test_write_maps_storage_recovery_to_the_recovery_region(void) {
    uint8_t data[4] = { 1, 2, 3, 4 };

    HWProxy_Storage_Write_ExpectAndReturn(HWPROXY_STORAGE_REGION_RECOVERY, 0, data, sizeof(data), HWPROXY_STORAGE_OK);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, Storage_Write(STORAGE_RECOVERY, 0, data, sizeof(data)));
}

void test_write_maps_storage_firmware_update_to_the_firmware_update_region(void) {
    uint8_t data[4] = { 5, 6, 7, 8 };

    HWProxy_Storage_Write_ExpectAndReturn(HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE, 100, data, sizeof(data), HWPROXY_STORAGE_OK);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, Storage_Write(STORAGE_FIRMWARE_UPDATE, 100, data, sizeof(data)));
}

void test_read_maps_storage_firmware_update_to_the_firmware_update_region(void) {
    uint8_t buffer[4];

    HWProxy_Storage_Read_ExpectAndReturn(HWPROXY_STORAGE_REGION_FIRMWARE_UPDATE, 0, buffer, sizeof(buffer), HWPROXY_STORAGE_ERROR);

    TEST_ASSERT_EQUAL_INT(STORAGE_ERROR, Storage_Read(STORAGE_FIRMWARE_UPDATE, 0, buffer, sizeof(buffer)));
}

void test_erase_maps_storage_recovery_to_the_recovery_region(void) {
    HWProxy_Storage_Erase_ExpectAndReturn(HWPROXY_STORAGE_REGION_RECOVERY, HWPROXY_STORAGE_OK);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, Storage_Erase(STORAGE_RECOVERY));
}

void test_recovery_and_firmware_update_reject_null_pointers_without_calling_the_proxy(void) {
    TEST_ASSERT_EQUAL_INT(STORAGE_INVALID, Storage_Read(STORAGE_RECOVERY, 0, NULL, 4));
    TEST_ASSERT_EQUAL_INT(STORAGE_INVALID, Storage_Write(STORAGE_FIRMWARE_UPDATE, 0, NULL, 4));
}
