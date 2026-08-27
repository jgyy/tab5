#include <unity.h>
#include "hittest.h"

void setUp(void) {}
void tearDown(void) {}

void test_point_inside_rect() {
    Rect r{10, 10, 50, 20};
    TEST_ASSERT_TRUE(rectContains(r, 20, 15));
}

void test_point_outside_rect() {
    Rect r{10, 10, 50, 20};
    TEST_ASSERT_FALSE(rectContains(r, 5, 15));
    TEST_ASSERT_FALSE(rectContains(r, 20, 35));
}

void test_left_top_edge_is_inclusive() {
    Rect r{10, 10, 50, 20};
    TEST_ASSERT_TRUE(rectContains(r, 10, 10));
}

void test_right_bottom_edge_is_exclusive() {
    Rect r{10, 10, 50, 20};
    TEST_ASSERT_FALSE(rectContains(r, 60, 10));
    TEST_ASSERT_FALSE(rectContains(r, 10, 30));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_point_inside_rect);
    RUN_TEST(test_point_outside_rect);
    RUN_TEST(test_left_top_edge_is_inclusive);
    RUN_TEST(test_right_bottom_edge_is_exclusive);
    return UNITY_END();
}
