#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_ceedling_setup_is_working(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

void test_unity_string_assertion_works(void) {
    TEST_ASSERT_EQUAL_STRING("saipa-tcu", "saipa-tcu");
}

void test_unity_true_false_assertions_work(void) {
    TEST_ASSERT_TRUE(1 == 1);
    TEST_ASSERT_FALSE(1 == 2);
}