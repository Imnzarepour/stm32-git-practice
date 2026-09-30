#include "unity.h"
#include "bootloader.h"
#include "Mockboot_hal.h"
#include "boot_control.h"
#include "boot_error.h"
#include "boot_image_header.h"
#include "boot_config.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Covers the scenario table in test/README.md, using CMock to mock
 * boot_hal.h - the same "mock the one boundary this file touches" style
 * firmware/test/unit/test_storage.c already uses for
 * hardware_proxy_storage.h and firmware/test/unit/test_system_management.c
 * uses for infrastructure/storage.h. There is no real target
 * implementation of boot_hal.h anywhere yet (no CubeIDE project for either
 * board's Bootloader), so this is currently the ONLY thing that exercises
 * bootloader.c.
 *
 * The stub callbacks below are this test file's entire "Fake hardware":
 * two RAM Flash Banks, one RAM EEPROM (STORAGE_RECOVERY layout), and one
 * RAM "New Image" staging buffer standing in for the still-undecided Firmware Update
 * source (boot_hal.h's own TODO(open-item) comment on NewImage_*). */

#define TEST_BANK_CAPACITY 512u

static uint8_t s_bank_storage[2][TEST_BANK_CAPACITY];
static uint8_t s_eeprom[64];
static uint8_t s_new_image[512];
static uint32_t s_new_image_size;

static bool s_eeprom_read_should_fail;
static int  s_eeprom_write_calls;

static int  s_flash_erase_calls;
static int  s_flash_program_call_count;
static bool s_flash_program_always_fails;
static int  s_flash_program_fail_at_call_number; /* 0 = never fail */

static bool   s_jump_called;
static BankId s_jumped_to_bank;

/* A real CRC32 (not a simulation) - test/README.md explicitly allows this
 * ("این می‌تونه Real باشه، نه Fake"): used both by the stub_crc_calculate()
 * callback below (reading straight from the Fake Flash buffer) and by the
 * write_valid_image_to_bank()/set_new_image() test helpers (building the
 * header's stored CRC), so both sides always agree by construction. */
static uint32_t crc32_compute(const uint8_t* data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i, bit;
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return ~crc;
}

/* --- boot_hal.h stub callbacks (this file's Fake hardware) --- */

static BootHalResult stub_flash_read(BankId bank, uint32_t offset, void* buffer, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    TEST_ASSERT_TRUE(offset + length <= TEST_BANK_CAPACITY);
    memcpy(buffer, &s_bank_storage[bank][offset], length);
    return BOOT_HAL_OK;
}

static BootHalResult stub_flash_program(BankId bank, uint32_t offset, const void* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_flash_program_call_count++;
    if (s_flash_program_always_fails) {
        return BOOT_HAL_ERROR;
    }
    if (s_flash_program_fail_at_call_number != 0 && s_flash_program_call_count == s_flash_program_fail_at_call_number) {
        return BOOT_HAL_ERROR; /* simulates a power loss during exactly this write */
    }
    TEST_ASSERT_TRUE(offset + length <= TEST_BANK_CAPACITY);
    memcpy(&s_bank_storage[bank][offset], data, length);
    return BOOT_HAL_OK;
}

static BootHalResult stub_flash_erase(BankId bank, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_flash_erase_calls++;
    memset(s_bank_storage[bank], 0xFF, TEST_BANK_CAPACITY); /* erased Flash reads as 0xFF - a real image header never matches BOOT_IMAGE_MAGIC by accident */
    return BOOT_HAL_OK;
}

static void stub_flash_jump(BankId bank, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_jump_called    = true;
    s_jumped_to_bank = bank;
    /* Real hardware never returns here - this test double does, so tests
     * can assert which Bank was chosen (same pattern as
     * firmware/test/unit/test_system_management.c's HWProxy_ResetMcu()
     * stub). */
}

static uint32_t stub_crc_calculate(BankId bank, uint32_t offset, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    TEST_ASSERT_TRUE(offset + length <= TEST_BANK_CAPACITY);
    return crc32_compute(&s_bank_storage[bank][offset], length);
}

static BootHalResult stub_eeprom_read(uint32_t offset, void* buffer, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    if (s_eeprom_read_should_fail) {
        return BOOT_HAL_ERROR;
    }
    TEST_ASSERT_TRUE(offset + length <= sizeof(s_eeprom));
    memcpy(buffer, &s_eeprom[offset], length);
    return BOOT_HAL_OK;
}

static BootHalResult stub_eeprom_write(uint32_t offset, const void* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_eeprom_write_calls++;
    TEST_ASSERT_TRUE(offset + length <= sizeof(s_eeprom));
    memcpy(&s_eeprom[offset], data, length);
    return BOOT_HAL_OK;
}

static BootHalResult stub_new_image_read(uint32_t offset, void* buffer, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    TEST_ASSERT_TRUE(offset + length <= s_new_image_size);
    memcpy(buffer, &s_new_image[offset], length);
    return BOOT_HAL_OK;
}

static uint32_t stub_new_image_get_size(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_new_image_size;
}

/* --- Test-only helpers (build up the Fake state, read it back) --- */

static void write_valid_image_to_bank(BankId bank, const uint8_t* payload, uint32_t payload_len) {
    BootImageHeader header;
    header.magic      = BOOT_IMAGE_MAGIC;
    header.image_size = payload_len;
    header.image_crc  = crc32_compute(payload, payload_len);
    memcpy(&s_bank_storage[bank][0], &header, sizeof(header));
    memcpy(&s_bank_storage[bank][sizeof(header)], payload, payload_len);
}

static void set_new_image(const uint8_t* payload, uint32_t payload_len) {
    BootImageHeader header;
    header.magic      = BOOT_IMAGE_MAGIC;
    header.image_size = payload_len;
    header.image_crc  = crc32_compute(payload, payload_len);
    memcpy(&s_new_image[0], &header, sizeof(header));
    memcpy(&s_new_image[sizeof(header)], payload, payload_len);
    s_new_image_size = (uint32_t)sizeof(header) + payload_len;
}

static void set_boot_control(BankId bank, BootAction action) {
    BootControl control;
    control.active_bank = bank;
    control.action       = action;
    memcpy(&s_eeprom[STORAGE_RECOVERY_BOOT_CONTROL_OFFSET], &control, sizeof(control));
}

static BootControl get_boot_control(void) {
    BootControl control;
    memcpy(&control, &s_eeprom[STORAGE_RECOVERY_BOOT_CONTROL_OFFSET], sizeof(control));
    return control;
}

static BootErrorRecord get_boot_error(void) {
    BootErrorRecord error;
    memcpy(&error, &s_eeprom[STORAGE_RECOVERY_BOOT_ERROR_OFFSET], sizeof(error));
    return error;
}

void setUp(void) {
    memset(s_bank_storage, 0, sizeof(s_bank_storage)); /* magic == 0, not BOOT_IMAGE_MAGIC -> every Bank starts invalid until a test says otherwise */
    memset(s_eeprom, 0, sizeof(s_eeprom));
    memset(s_new_image, 0, sizeof(s_new_image));
    s_new_image_size = 0u;

    s_eeprom_read_should_fail = false;
    s_eeprom_write_calls      = 0;

    s_flash_erase_calls              = 0;
    s_flash_program_call_count       = 0;
    s_flash_program_always_fails     = false;
    s_flash_program_fail_at_call_number = 0;

    s_jump_called    = false;
    s_jumped_to_bank = BANK_1;

    Flash_Read_StubWithCallback(stub_flash_read);
    Flash_Program_StubWithCallback(stub_flash_program);
    Flash_Erase_StubWithCallback(stub_flash_erase);
    Flash_JumpToApplication_StubWithCallback(stub_flash_jump);
    CRC_Calculate_StubWithCallback(stub_crc_calculate);
    EEPROM_Read_StubWithCallback(stub_eeprom_read);
    EEPROM_Write_StubWithCallback(stub_eeprom_write);
    NewImage_Read_StubWithCallback(stub_new_image_read);
    NewImage_GetSize_StubWithCallback(stub_new_image_get_size);

    set_boot_control(BANK_1, BOOT_ACTION_BOOT); /* steady default - individual tests override before calling Bootloader_Run() */
}

void tearDown(void) {}

static const uint8_t k_payload[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };

/* --- BOOT: Active Bank CRC valid (bootloader.md section 4) --- */

void test_valid_active_bank_jumps_to_application(void) {
    write_valid_image_to_bank(BANK_1, k_payload, sizeof(k_payload));
    set_boot_control(BANK_1, BOOT_ACTION_BOOT);

    Bootloader_Run();

    TEST_ASSERT_TRUE_MESSAGE(s_jump_called, "a valid Active Bank must be jumped into");
    TEST_ASSERT_EQUAL_INT(BANK_1, s_jumped_to_bank);
}

/* --- BOOT: Active Bank CRC invalid (worst case, section 4) --- */

void test_invalid_active_bank_stays_in_bootloader_and_records_an_error(void) {
    /* BANK_1 left blank/invalid by setUp() on purpose. */
    set_boot_control(BANK_1, BOOT_ACTION_BOOT);

    Bootloader_Run();

    TEST_ASSERT_FALSE_MESSAGE(s_jump_called, "an invalid image must never be jumped into");

    BootErrorRecord error = get_boot_error();
    TEST_ASSERT_EQUAL_UINT32(BOOT_ERROR_TYPE_ACTIVE_BANK_INVALID, error.error_type);
    TEST_ASSERT_EQUAL_UINT32(BANK_1, error.error_code);
}

/* --- PROGRAM: successful Programming (sections 5-8, 11) --- */

void test_successful_programming_switches_active_bank_and_then_boots_it(void) {
    write_valid_image_to_bank(BANK_1, k_payload, sizeof(k_payload)); /* current Active - must stay untouched */
    set_new_image(k_payload, sizeof(k_payload));
    set_boot_control(BANK_1, BOOT_ACTION_PROGRAM);

    Bootloader_Run();

    BootControl control = get_boot_control();
    TEST_ASSERT_EQUAL_INT_MESSAGE(BANK_2, control.active_bank, "Bank 2 (the Inactive one) must become Active after a successful Program+Verify");
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_BOOT, control.action);

    TEST_ASSERT_TRUE_MESSAGE(s_jump_called, "Bootloader_Run() re-evaluates BOOT after a successful Program, in the same call");
    TEST_ASSERT_EQUAL_INT(BANK_2, s_jumped_to_bank);
}

/* --- PROGRAM: a write fails partway through, but a later Retry recovers
 * cleanly (section 9) - proves a partial/"power loss" write never leaves
 * the Inactive Bank silently half-written and mistaken for valid, and
 * never touches the Active Bank at all. --- */

void test_a_failed_write_is_safely_retried_and_can_still_succeed(void) {
    static const uint8_t two_chunk_payload[BOOT_COPY_CHUNK_BYTES + 10u] = { 0 };

    write_valid_image_to_bank(BANK_1, k_payload, sizeof(k_payload));
    set_new_image(two_chunk_payload, sizeof(two_chunk_payload)); /* header + this payload spans 2 chunks */
    set_boot_control(BANK_1, BOOT_ACTION_PROGRAM);

    s_flash_program_fail_at_call_number = 2; /* fails exactly the 2nd chunk write of the 1st attempt */

    Bootloader_Run();

    BootControl control = get_boot_control();
    TEST_ASSERT_EQUAL_INT_MESSAGE(BANK_2, control.active_bank, "the Retry (2nd attempt, after a fresh Erase) must still be able to succeed");
    TEST_ASSERT_TRUE(s_jump_called);
    TEST_ASSERT_EQUAL_INT(BANK_2, s_jumped_to_bank);
}

/* --- PROGRAM: every Retry fails (section 9) - must fall back to the
 * previous Active Bank untouched, and record a Boot Error. --- */

void test_retries_exhausted_falls_back_to_the_previous_active_bank(void) {
    write_valid_image_to_bank(BANK_1, k_payload, sizeof(k_payload));
    set_new_image(k_payload, sizeof(k_payload));
    set_boot_control(BANK_1, BOOT_ACTION_PROGRAM);

    s_flash_program_always_fails = true;

    Bootloader_Run();

    BootControl control = get_boot_control();
    TEST_ASSERT_EQUAL_INT_MESSAGE(BANK_1, control.active_bank, "Fail-Safe (section 10): the previous Active Bank must never change on total failure");
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_BOOT, control.action);

    BootErrorRecord error = get_boot_error();
    TEST_ASSERT_EQUAL_UINT32(BOOT_ERROR_TYPE_PROGRAMMING_FAILED, error.error_type);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(BOOT_MAX_PROGRAM_RETRIES, error.error_code, "error_code carries how many attempts were made");

    TEST_ASSERT_TRUE_MESSAGE(s_jump_called, "the still-valid previous Active Bank must still boot normally after the failed Update");
    TEST_ASSERT_EQUAL_INT(BANK_1, s_jumped_to_bank);
}

/* --- PROGRAM: nothing to program (sections 6/9 - a Boot Action of
 * PROGRAM with no New Image available must not crash or Erase anything,
 * it is just an immediate failed attempt like any other). --- */

void test_no_new_image_available_is_treated_as_a_failed_attempt(void) {
    write_valid_image_to_bank(BANK_1, k_payload, sizeof(k_payload));
    set_boot_control(BANK_1, BOOT_ACTION_PROGRAM);
    /* s_new_image_size left at 0 by setUp() - nothing staged. */

    Bootloader_Run();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_flash_erase_calls, "must not Erase the Inactive Bank when there is nothing to write");

    BootControl control = get_boot_control();
    TEST_ASSERT_EQUAL_INT(BANK_1, control.active_bank);
    TEST_ASSERT_TRUE(s_jump_called);
    TEST_ASSERT_EQUAL_INT(BANK_1, s_jumped_to_bank);
}

/* --- PROGRAM: a New Image larger than one Bank must be rejected the same
 * way, before touching Flash at all. --- */

void test_oversized_new_image_is_rejected_without_erasing_flash(void) {
    write_valid_image_to_bank(BANK_1, k_payload, sizeof(k_payload));
    set_boot_control(BANK_1, BOOT_ACTION_PROGRAM);
    s_new_image_size = BOOT_BANK_SIZE_BYTES + 1u; /* bigger than a Bank can hold - no backing bytes needed, Erase must never be reached */

    Bootloader_Run();

    TEST_ASSERT_EQUAL_INT(0, s_flash_erase_calls);
    TEST_ASSERT_EQUAL_INT(BANK_1, get_boot_control().active_bank);
}

/* --- First boot / unreadable EEPROM (no formal Contract default exists -
 * see bootloader.c's own comment on this fallback). ---
 *
 * Fixed: the default used to live only in RAM (asserted here as
 * s_eeprom_write_calls == 0), which meant a brand-new device's very first
 * real Firmware Update Install attempt always failed - the Application's
 * own pre-PROGRAM read of STORAGE_RECOVERY (preserving active_bank) never
 * found a real record. Now the assumed default is persisted immediately. */

void test_unreadable_eeprom_defaults_to_bank_1_boot_and_persists_it(void) {
    write_valid_image_to_bank(BANK_1, k_payload, sizeof(k_payload));
    s_eeprom_read_should_fail = true;

    Bootloader_Run();

    TEST_ASSERT_TRUE(s_jump_called);
    TEST_ASSERT_EQUAL_INT(BANK_1, s_jumped_to_bank);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_eeprom_write_calls,
        "the first-boot default must now be persisted, so a later reader never hits this same fallback again");

    BootControl persisted = get_boot_control();
    TEST_ASSERT_EQUAL_INT(BANK_1, persisted.active_bank);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_BOOT, persisted.action);
}
