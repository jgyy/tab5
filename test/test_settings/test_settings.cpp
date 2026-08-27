#include <unity.h>
#include "settings.h"

void setUp(void) {}
void tearDown(void) {}

void test_clamp_brightness_stays_at_or_above_minimum(void) {
    TEST_ASSERT_EQUAL_UINT8(kMinBrightness, clampBrightness(0));
    TEST_ASSERT_EQUAL_UINT8(kMinBrightness, clampBrightness(-100));
    TEST_ASSERT_EQUAL_UINT8(kMinBrightness, clampBrightness(kMinBrightness));
}

void test_clamp_brightness_stays_at_or_below_maximum_without_wrapping(void) {
    // 240 + kSettingsStep(32) = 272, which would wrap to 16 if done in raw uint8_t
    // arithmetic instead of clamped int arithmetic - this is exactly the bug clampBrightness
    // exists to prevent.
    TEST_ASSERT_EQUAL_UINT8(kMaxBrightness, clampBrightness(240 + kSettingsStep));
    TEST_ASSERT_EQUAL_UINT8(kMaxBrightness, clampBrightness(1000));
}

void test_clamp_brightness_passes_through_mid_range_values(void) {
    TEST_ASSERT_EQUAL_UINT8(150, clampBrightness(150));
}

void test_clamp_volume_allows_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, clampVolume(0));
    TEST_ASSERT_EQUAL_UINT8(0, clampVolume(-50));
}

void test_clamp_volume_stays_at_or_below_maximum_without_wrapping(void) {
    TEST_ASSERT_EQUAL_UINT8(kMaxVolume, clampVolume(240 + kSettingsStep));
    TEST_ASSERT_EQUAL_UINT8(kMaxVolume, clampVolume(1000));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_clamp_brightness_stays_at_or_above_minimum);
    RUN_TEST(test_clamp_brightness_stays_at_or_below_maximum_without_wrapping);
    RUN_TEST(test_clamp_brightness_passes_through_mid_range_values);
    RUN_TEST(test_clamp_volume_allows_zero);
    RUN_TEST(test_clamp_volume_stays_at_or_below_maximum_without_wrapping);
    return UNITY_END();
}
