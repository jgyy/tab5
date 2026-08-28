#include <unity.h>
#include "hash.h"

void setUp(void) {}
void tearDown(void) {}

void test_hash_unit_float_is_deterministic(void) {
    float a = hashUnitFloat(3, 7);
    float b = hashUnitFloat(3, 7);
    TEST_ASSERT_EQUAL_FLOAT(a, b);
}

void test_hash_unit_float_is_within_unit_range(void) {
    for (int i = 0; i < 20; ++i) {
        float v = hashUnitFloat(i, i * 3 + 1);
        TEST_ASSERT_TRUE(v >= 0.0f);
        TEST_ASSERT_TRUE(v < 1.0f);
    }
}

void test_hash_unit_float_differs_across_inputs(void) {
    float a = hashUnitFloat(1, 1);
    float b = hashUnitFloat(1, 2);
    TEST_ASSERT_TRUE(a != b);
}

void test_hash_range_maps_into_bounds(void) {
    for (int i = 0; i < 20; ++i) {
        float v = hashRange(i, 5, -2.0f, 3.0f);
        TEST_ASSERT_TRUE(v >= -2.0f);
        TEST_ASSERT_TRUE(v < 3.0f);
    }
}

void test_hash_range_is_deterministic(void) {
    float a = hashRange(9, 2, 0.0f, 10.0f);
    float b = hashRange(9, 2, 0.0f, 10.0f);
    TEST_ASSERT_EQUAL_FLOAT(a, b);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_hash_unit_float_is_deterministic);
    RUN_TEST(test_hash_unit_float_is_within_unit_range);
    RUN_TEST(test_hash_unit_float_differs_across_inputs);
    RUN_TEST(test_hash_range_maps_into_bounds);
    RUN_TEST(test_hash_range_is_deterministic);
    return UNITY_END();
}
