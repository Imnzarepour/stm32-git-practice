#include "unity.h"
#include "shared/command_safety_policy.h"

/* Pure, deterministic, no dependencies - real logic under test, same as
 * shared/event_policy.c's POLICY_TABLE. See command_safety_policy.h for
 * the explicit user decision this enforces. */

void setUp(void) {}
void tearDown(void) {}

void test_diagnostic_domain_is_allowed(void) {
    TEST_ASSERT_TRUE(CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_DIAGNOSTIC));
}

void test_gps_position_read_domain_is_allowed(void) {
    TEST_ASSERT_TRUE(CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_GPS_POSITION_READ));
}

void test_firmware_download_domain_is_allowed(void) {
    TEST_ASSERT_TRUE(CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_FIRMWARE_DOWNLOAD));
}

void test_diagnostic_actuator_test_domain_is_allowed(void) {
    /* Explicit, deliberate user decision - this ONE domain physically
     * actuates the vehicle (Door Lock/Unlock), unlike every other domain
     * above. See the header's comment on this case for the full
     * reasoning - Pass-Through by explicit request, not by this file's
     * own judgment. */
    TEST_ASSERT_TRUE(CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST));
}

void test_actuator_door_gpio_domain_is_allowed(void) {
    /* Explicit, deliberate user decision - second Actuating domain,
     * separate mechanism (direct GPIO relay) for the same physical
     * action as COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST. See the header's
     * comment on this case for the full reasoning - Pass-Through by
     * explicit request, not by this file's own judgment. */
    TEST_ASSERT_TRUE(CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO));
}

void test_firmware_install_domain_is_allowed(void) {
    /* Explicit, deliberate user decision, given by direct analogy to the
     * Door Lock/Unlock domains above ("مثل درب باز کن") plus a concrete
     * description of the intended mechanism. UNLIKE the two Actuator
     * domains above, accepting this Command writes Boot Action=PROGRAM
     * into STORAGE_RECOVERY and issues a real SYSTEM_RESET - see the
     * header's comment on this case for the full reasoning and its
     * still-open limits (no real package validation before this point). */
    TEST_ASSERT_TRUE(CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_FIRMWARE_INSTALL));
}

void test_unknown_domain_is_fail_closed(void) {
    TEST_ASSERT_FALSE(CommandSafetyPolicy_IsAllowed((CommandDomain)99));
}
