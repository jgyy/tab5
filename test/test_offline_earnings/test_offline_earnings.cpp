#include <unity.h>
#include "offline_earnings.h"

void setUp(void) {}
void tearDown(void) {}

void test_normal_elapsed_time_grants_expected_qi() {
    double earned = computeOfflineEarnings(1000, 900, 2.0, 86400);
    TEST_ASSERT_EQUAL_DOUBLE(200.0, earned);
}

void test_clock_moved_backward_grants_zero() {
    double earned = computeOfflineEarnings(500, 900, 2.0, 86400);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, earned);
}

void test_elapsed_time_is_capped() {
    double earned = computeOfflineEarnings(100000, 0, 1.0, 3600);
    TEST_ASSERT_EQUAL_DOUBLE(3600.0, earned);
}

void test_zero_elapsed_grants_zero() {
    double earned = computeOfflineEarnings(1000, 1000, 5.0, 86400);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, earned);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_normal_elapsed_time_grants_expected_qi);
    RUN_TEST(test_clock_moved_backward_grants_zero);
    RUN_TEST(test_elapsed_time_is_capped);
    RUN_TEST(test_zero_elapsed_grants_zero);
    return UNITY_END();
}
