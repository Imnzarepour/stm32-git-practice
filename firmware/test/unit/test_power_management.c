#include "unity.h"
#include "managements/power_management.h"
#include "Mockhardware_input_management.h"

/* Power Management's only dependency is Hardware Input's ACC reading,
 * consumed via the Data path (GetDigitalInput). Mocking that header
 * keeps this test isolated from Hardware Input's own debounce/EMA
 * internals - those are covered by test_hardware_input_management.c. */

void setUp(void) {
    Power_Init();
}

void tearDown(void) {}

void test_default_mode_is_low_power_before_any_process(void) {
    /* Conservative, low-consumption default until the real ACC status is
     * known (decided by the user - previously an open question). */
    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());
}

void test_acc_on_selects_normal(void) {
    DigitalInputValue acc = { true, true, 0 };
    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc);

    Power_Process();

    TEST_ASSERT_EQUAL_INT(POWER_NORMAL, Power_GetMode());
}

void test_acc_off_selects_low_power(void) {
    DigitalInputValue acc = { false, true, 0 };
    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc);

    Power_Process();

    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());
}

void test_invalid_acc_keeps_the_previous_mode(void) {
    DigitalInputValue acc_off = { false, true, 0 };
    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_off);
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());

    DigitalInputValue acc_invalid = { true, false, 0 };
    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_invalid);
    Power_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(POWER_LOW_POWER, Power_GetMode(), "An untrustworthy ACC reading must not change the Mode");
}

void test_mode_tracks_repeated_acc_transitions(void) {
    DigitalInputValue acc_on  = { true, true, 0 };
    DigitalInputValue acc_off = { false, true, 0 };

    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_on);
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL, Power_GetMode());

    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_off);
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());

    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_on);
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL, Power_GetMode());
}

void test_invalid_acc_on_first_process_keeps_the_init_default(void) {
    /* No real ACC reading has ever been observed yet (setUp() only called
     * Power_Init()) - an untrustworthy reading right at startup must keep
     * whatever Power_Init() set, not fabricate a Mode from garbage data. */
    DigitalInputValue acc_invalid = { true, false, 0 };
    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_invalid);

    Power_Process();

    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());
}

void test_repeated_process_with_same_acc_value_is_idempotent(void) {
    DigitalInputValue acc_on = { true, true, 0 };

    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_on);
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL, Power_GetMode());

    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_on);
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL, Power_GetMode());
}

void test_getmode_has_no_side_effects(void) {
    /* Power_GetMode() must be a pure read: repeated calls without an
     * intervening Power_Process() must never touch Hardware Input (no
     * mock expectation set up here - CMock will fail the test if
     * GetDigitalInput() is called unexpectedly) and must keep returning
     * the same value. */
    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());
    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());
    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());
}

void test_reinit_resets_mode_to_default(void) {
    DigitalInputValue acc_on = { true, true, 0 };
    GetDigitalInput_ExpectAndReturn(DIGITAL_INPUT_ACC, acc_on);
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL, Power_GetMode());

    /* Re-Init (e.g. after a Reset/restart cycle) must not retain state
     * from the previous run - Mode has to go back to the same conservative
     * default Power_Init() establishes on a cold start (LOW_POWER), even
     * though the Mode right before Re-Init was NORMAL. */
    Power_Init();

    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());
}
