#include <unity.h>
#include "fx.h"

void setUp(void) {}
void tearDown(void) {}

void test_shake_offset_is_zero_at_start(void) {
    float v = shakeOffset(0.0f, 5.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_shake_offset_is_zero_at_end(void) {
    float v = shakeOffset(1.0f, 5.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_shake_offset_stays_within_amplitude(void) {
    for (int i = 0; i <= 100; ++i) {
        float t = static_cast<float>(i) / 100.0f;
        float v = shakeOffset(t, 5.0f, 0.0f);
        TEST_ASSERT_TRUE(v >= -5.0001f && v <= 5.0001f);
    }
}

void test_shake_offset_phase_shift_differs_from_unshifted(void) {
    float a = shakeOffset(0.3f, 5.0f, 0.0f);
    float b = shakeOffset(0.3f, 5.0f, 1.5707963f); // pi/2
    TEST_ASSERT_TRUE(a != b);
}

void test_damage_number_rise_offset_is_zero_at_spawn(void) {
    float v = damageNumberRiseOffsetPx(0.0f, 40.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_damage_number_rise_offset_grows_more_negative_over_time(void) {
    float early = damageNumberRiseOffsetPx(0.1f, 40.0f);
    float late = damageNumberRiseOffsetPx(0.5f, 40.0f);
    TEST_ASSERT_TRUE(late < early);
}

void test_parallax_wrap_x_stays_within_viewport(void) {
    for (int i = 0; i < 50; ++i) {
        float elapsed = static_cast<float>(i) * 3.7f; // sweep past multiple wraps
        float v = parallaxWrapX(10.0f, 20.0f, elapsed, 200.0f);
        TEST_ASSERT_TRUE(v >= 0.0f && v < 200.0f);
    }
}

void test_parallax_wrap_x_is_deterministic(void) {
    float a = parallaxWrapX(10.0f, 20.0f, 12.5f, 200.0f);
    float b = parallaxWrapX(10.0f, 20.0f, 12.5f, 200.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a, b);
}

void test_pulse_envelope_is_zero_at_start(void) {
    float v = pulseEnvelope(0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_pulse_envelope_is_zero_at_end(void) {
    float v = pulseEnvelope(1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_pulse_envelope_peaks_near_midpoint(void) {
    float v = pulseEnvelope(0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, v);
}

void test_pulse_envelope_stays_within_unit_range(void) {
    for (int i = 0; i <= 100; ++i) {
        float t = static_cast<float>(i) / 100.0f;
        float v = pulseEnvelope(t);
        TEST_ASSERT_TRUE(v >= -0.0001f && v <= 1.0001f);
    }
}

void test_pulse_envelope_clamps_out_of_range_input(void) {
    float belowStart = pulseEnvelope(-0.5f);
    float atStart = pulseEnvelope(0.0f);
    float pastEnd = pulseEnvelope(1.5f);
    float atEnd = pulseEnvelope(1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, atStart, belowStart);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, atEnd, pastEnd);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_shake_offset_is_zero_at_start);
    RUN_TEST(test_shake_offset_is_zero_at_end);
    RUN_TEST(test_shake_offset_stays_within_amplitude);
    RUN_TEST(test_shake_offset_phase_shift_differs_from_unshifted);
    RUN_TEST(test_damage_number_rise_offset_is_zero_at_spawn);
    RUN_TEST(test_damage_number_rise_offset_grows_more_negative_over_time);
    RUN_TEST(test_parallax_wrap_x_stays_within_viewport);
    RUN_TEST(test_parallax_wrap_x_is_deterministic);
    RUN_TEST(test_pulse_envelope_is_zero_at_start);
    RUN_TEST(test_pulse_envelope_is_zero_at_end);
    RUN_TEST(test_pulse_envelope_peaks_near_midpoint);
    RUN_TEST(test_pulse_envelope_stays_within_unit_range);
    RUN_TEST(test_pulse_envelope_clamps_out_of_range_input);
    return UNITY_END();
}
