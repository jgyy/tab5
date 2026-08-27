#include <unity.h>
#include "raycast.h"

void setUp(void) {}
void tearDown(void) {}

RaycastMap makeTestMap() {
    // 5x5, walls (type 1) around the border, open floor inside.
    RaycastMap m;
    m.width = 5;
    m.height = 5;
    m.cells = {
        1,1,1,1,1,
        1,0,0,0,1,
        1,0,0,0,1,
        1,0,0,0,1,
        1,1,1,1,1,
    };
    return m;
}

void test_ray_hits_wall_straight_ahead(void) {
    RaycastMap m = makeTestMap();
    // Origin at cell center (2.5, 2.5), firing straight in +X: hits the wall at x=4
    // (the wall cell spans x in [4,5)), so distance should be 4.0 - 2.5 = 1.5.
    RayHit hit = castRay(m, 2.5f, 2.5f, 1.0f, 0.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, hit.distance);
    TEST_ASSERT_EQUAL_INT(1, hit.wallType);
    TEST_ASSERT_TRUE(hit.hitVertical);
}

void test_ray_hits_wall_straight_down(void) {
    RaycastMap m = makeTestMap();
    RayHit hit = castRay(m, 2.5f, 2.5f, 0.0f, 1.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, hit.distance);
    TEST_ASSERT_FALSE(hit.hitVertical);
}

void test_ray_wallx_is_fractional_hit_position(void) {
    RaycastMap m = makeTestMap();
    // Same straight-ahead ray as above hits the wall face at y=2.5 exactly -> wallX = 0.5
    // (the fractional position along the hit cell's edge).
    RayHit hit = castRay(m, 2.5f, 2.5f, 1.0f, 0.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, hit.wallX);
}

void test_ray_beyond_max_distance_returns_no_hit(void) {
    RaycastMap m = makeTestMap();
    RayHit hit = castRay(m, 2.5f, 2.5f, 1.0f, 0.0f, 1.0f); // wall is 1.5 away, cap at 1.0
    TEST_ASSERT_EQUAL_INT(0, hit.wallType);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, hit.distance);
}

void test_cast_columns_fills_one_hit_per_column(void) {
    RaycastMap m = makeTestMap();
    std::vector<WallHit> hits;
    castColumns(m, 2.5f, 2.5f, 0.0f, 1.0f, 7, 20.0f, hits);
    TEST_ASSERT_EQUAL_INT(7, static_cast<int>(hits.size()));
    for (const auto& h : hits) {
        TEST_ASSERT_TRUE(h.wallType > 0);
    }
}

void test_cast_columns_center_ray_matches_facing_direction(void) {
    RaycastMap m = makeTestMap();
    std::vector<WallHit> hits;
    // facing straight in +X (radians 0), odd screenWidth so the middle column is the exact
    // camera-forward ray with no fisheye correction needed (correction factor cos(0) == 1).
    castColumns(m, 2.5f, 2.5f, 0.0f, 1.0f, 5, 20.0f, hits);
    WallHit center = hits[2];
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, center.distance);
}

void test_wall_slice_height_shrinks_with_distance(void) {
    int near = wallSliceHeight(1.0f, 240);
    int far = wallSliceHeight(4.0f, 240);
    TEST_ASSERT_TRUE(near > far);
    TEST_ASSERT_EQUAL_INT(240, near);  // distance 1.0 -> full viewport height
    TEST_ASSERT_EQUAL_INT(60, far);    // distance 4.0 -> quarter height
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ray_hits_wall_straight_ahead);
    RUN_TEST(test_ray_hits_wall_straight_down);
    RUN_TEST(test_ray_wallx_is_fractional_hit_position);
    RUN_TEST(test_ray_beyond_max_distance_returns_no_hit);
    RUN_TEST(test_cast_columns_fills_one_hit_per_column);
    RUN_TEST(test_cast_columns_center_ray_matches_facing_direction);
    RUN_TEST(test_wall_slice_height_shrinks_with_distance);
    return UNITY_END();
}
