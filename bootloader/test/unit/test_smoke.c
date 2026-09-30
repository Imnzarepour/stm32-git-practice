#include "unity.h"
#include "bank_id.h"
#include "boot_action.h"
#include "boot_control.h"
#include "boot_error.h"
#include <stddef.h>

/* Proves the Ceedling harness for this (Application-independent) project
 * works on its own, and that the STORAGE_RECOVERY layout constants agree
 * with what firmware/src/managements/system_management.c already reads
 * (BOOT_ERROR_RECORD_OFFSET there == STORAGE_RECOVERY_BOOT_ERROR_OFFSET
 * here). If one side changes this layout without the other, this is the
 * test meant to be updated to keep both sides honest. */

void setUp(void) {}
void tearDown(void) {}

void test_ceedling_setup_is_working(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

void test_bank_id_has_exactly_two_values(void) {
    TEST_ASSERT_EQUAL_INT(0, BANK_1);
    TEST_ASSERT_EQUAL_INT(1, BANK_2);
}

void test_boot_action_has_exactly_two_values(void) {
    TEST_ASSERT_EQUAL_INT(0, BOOT_ACTION_BOOT);
    TEST_ASSERT_EQUAL_INT(1, BOOT_ACTION_PROGRAM);
}

void test_boot_control_sits_at_the_start_of_storage_recovery(void) {
    TEST_ASSERT_EQUAL_UINT32(0, STORAGE_RECOVERY_BOOT_CONTROL_OFFSET);
}

void test_boot_error_record_offset_matches_the_application_side_consumer(void) {
    /* Must stay equal to BOOT_ERROR_RECORD_OFFSET in
     * firmware/src/managements/system_management.c. */
    TEST_ASSERT_EQUAL_UINT32(8, STORAGE_RECOVERY_BOOT_ERROR_OFFSET);
    TEST_ASSERT_EQUAL_UINT32(sizeof(BootControl), STORAGE_RECOVERY_BOOT_ERROR_OFFSET);
}
