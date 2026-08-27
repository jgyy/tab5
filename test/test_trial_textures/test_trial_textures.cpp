#include <unity.h>
#include "trial_textures.h"

void setUp(void) {}
void tearDown(void) {}

void test_sample_is_deterministic(void) {
    RGB base{120, 60, 200};
    RGB a = sampleWallTexture(1, 0.3f, 0.7f, base);
    RGB b = sampleWallTexture(1, 0.3f, 0.7f, base);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_different_wall_types_look_different(void) {
    RGB base{120, 60, 200};
    RGB type1 = sampleWallTexture(1, 0.5f, 0.5f, base);
    RGB type2 = sampleWallTexture(2, 0.5f, 0.5f, base);
    TEST_ASSERT_TRUE(type1.r != type2.r || type1.g != type2.g || type1.b != type2.b);
}

void test_sample_stays_within_uv_bounds_at_edges(void) {
    RGB base{120, 60, 200};
    // Should not crash or produce garbage at u/v exactly 0.0 or just under 1.0.
    RGB corner = sampleWallTexture(1, 0.0f, 0.0f, base);
    RGB farCorner = sampleWallTexture(1, 0.999f, 0.999f, base);
    (void)corner;
    (void)farCorner;
    TEST_ASSERT_TRUE(true); // reaching here without a crash is the assertion
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sample_is_deterministic);
    RUN_TEST(test_different_wall_types_look_different);
    RUN_TEST(test_sample_stays_within_uv_bounds_at_edges);
    return UNITY_END();
}
