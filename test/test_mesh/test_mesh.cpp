#include <unity.h>
#include "mesh.h"

void setUp(void) {}
void tearDown(void) {}

void test_icosahedron_has_12_vertices_and_20_faces() {
    Mesh m = makeIcosahedron();
    TEST_ASSERT_EQUAL(12, m.vertices.size());
    TEST_ASSERT_EQUAL(20, m.faces.size());
}

void test_icosahedron_vertices_are_unit_length() {
    Mesh m = makeIcosahedron();
    for (const auto& v : m.vertices) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, v.length());
    }
}

void test_subdivide_quadruples_face_count() {
    Mesh m = makeIcosahedron();
    Mesh sub = subdivide(m);
    TEST_ASSERT_EQUAL(80, sub.faces.size());
}

void test_subdivide_new_vertices_stay_unit_length() {
    Mesh m = makeIcosahedron();
    Mesh sub = subdivide(m);
    for (const auto& v : sub.vertices) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, v.length());
    }
}

void test_growForRealm_is_deterministic() {
    Mesh base = makeIcosahedron();
    RealmVisual a = growForRealm(base, 3);
    RealmVisual b = growForRealm(base, 3);
    TEST_ASSERT_EQUAL(a.mesh.vertices.size(), b.mesh.vertices.size());
    for (size_t i = 0; i < a.mesh.vertices.size(); ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.mesh.vertices[i].x, b.mesh.vertices[i].x);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.mesh.vertices[i].y, b.mesh.vertices[i].y);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.mesh.vertices[i].z, b.mesh.vertices[i].z);
    }
}

void test_growForRealm_jaggedness_increases_with_realm() {
    Mesh base = makeIcosahedron();
    RealmVisual low = growForRealm(base, 0);
    RealmVisual high = growForRealm(base, NUM_REALMS - 1);

    float lowTotalDisplacement = 0.0f;
    float highTotalDisplacement = 0.0f;
    for (size_t i = 0; i < base.vertices.size(); ++i) {
        lowTotalDisplacement += (low.mesh.vertices[i] - base.vertices[i]).length();
        highTotalDisplacement += (high.mesh.vertices[i] - base.vertices[i]).length();
    }
    TEST_ASSERT_TRUE(highTotalDisplacement > lowTotalDisplacement);
}

void test_growForRealm_returns_expected_palette_per_realm() {
    Mesh base = makeIcosahedron();
    RealmVisual mortal = growForRealm(base, 0);
    RealmVisual voidRefinement = growForRealm(base, NUM_REALMS - 1);
    TEST_ASSERT_EQUAL_UINT8(220, mortal.baseColor.r);
    TEST_ASSERT_EQUAL_UINT8(220, mortal.baseColor.g);
    TEST_ASSERT_EQUAL_UINT8(220, mortal.baseColor.b);
    TEST_ASSERT_EQUAL_UINT8(255, voidRefinement.baseColor.r);
    TEST_ASSERT_EQUAL_UINT8(245, voidRefinement.baseColor.g);
    TEST_ASSERT_EQUAL_UINT8(210, voidRefinement.baseColor.b);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_icosahedron_has_12_vertices_and_20_faces);
    RUN_TEST(test_icosahedron_vertices_are_unit_length);
    RUN_TEST(test_subdivide_quadruples_face_count);
    RUN_TEST(test_subdivide_new_vertices_stay_unit_length);
    RUN_TEST(test_growForRealm_is_deterministic);
    RUN_TEST(test_growForRealm_jaggedness_increases_with_realm);
    RUN_TEST(test_growForRealm_returns_expected_palette_per_realm);
    return UNITY_END();
}
