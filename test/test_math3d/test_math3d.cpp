// test/test_math3d/test_math3d.cpp
#include <unity.h>
#include "math3d.h"

void setUp(void) {}
void tearDown(void) {}

void test_vec3_add() {
    Vec3 a{1, 2, 3};
    Vec3 b{4, 5, 6};
    Vec3 r = a + b;
    TEST_ASSERT_EQUAL_FLOAT(5.0f, r.x);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, r.y);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, r.z);
}

void test_vec3_dot() {
    Vec3 a{1, 0, 0};
    Vec3 b{0, 1, 0};
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.dot(b));
    Vec3 c{2, 3, 4};
    TEST_ASSERT_EQUAL_FLOAT(4.0f + 9.0f + 16.0f, c.dot(c));
}

void test_vec3_cross_of_axes() {
    Vec3 x{1, 0, 0};
    Vec3 y{0, 1, 0};
    Vec3 r = x.cross(y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.y);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.z);
}

void test_vec3_normalized() {
    Vec3 v{3, 4, 0};
    Vec3 n = v.normalized();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, n.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, n.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, n.length());
}

void test_mat4_rotationY_quarter_turn() {
    Mat4 r = Mat4::rotationY(static_cast<float>(M_PI) / 2.0f);
    Vec3 p{1, 0, 0};
    Vec3 rotated = r.transformPoint(p);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rotated.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rotated.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, rotated.z);
}

void test_mat4_translation() {
    Mat4 t = Mat4::translation(1, 2, 3);
    Vec3 p{0, 0, 0};
    Vec3 r = t.transformPoint(p);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.x);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, r.y);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, r.z);
}

void test_project_center_point() {
    Vec3 p{0, 0, 5};
    ProjectedPoint proj = project(p, 1.5f, 320, 240);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 160.0f, proj.screenX);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, proj.screenY);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, proj.depth);
}

void test_project_point_offset_right_and_up() {
    Vec3 p{1, 1, 5};
    ProjectedPoint proj = project(p, 1.5f, 320, 240);
    TEST_ASSERT_TRUE(proj.screenX > 160.0f);
    TEST_ASSERT_TRUE(proj.screenY < 120.0f);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_vec3_add);
    RUN_TEST(test_vec3_dot);
    RUN_TEST(test_vec3_cross_of_axes);
    RUN_TEST(test_vec3_normalized);
    RUN_TEST(test_mat4_rotationY_quarter_turn);
    RUN_TEST(test_mat4_translation);
    RUN_TEST(test_project_center_point);
    RUN_TEST(test_project_point_offset_right_and_up);
    return UNITY_END();
}
