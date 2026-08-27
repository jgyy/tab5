#include <unity.h>
#include "framebuffer.h"
#include "rasterizer.h"
#include "mesh.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
Mesh singleTriangle(bool frontFacing) {
    Mesh m;
    if (frontFacing) {
        m.vertices = { {-1, -1, 0}, {0, 1, 0}, {1, -1, 0} };
    } else {
        m.vertices = { {-1, -1, 0}, {1, -1, 0}, {0, 1, 0} };
    }
    m.faces = { {0, 1, 2} };
    return m;
}

RasterParams defaultParams() {
    RasterParams p;
    p.transform = Mat4::identity();
    p.cameraDistance = 5.0f;
    p.focalLength = 1.5f;
    p.lightDir = Vec3{0, 0, -1}.normalized();
    p.viewDir = Vec3{0, 0, -1}.normalized();
    p.baseColor = RGB{200, 50, 50};
    p.rimColor = RGB{255, 255, 255};
    return p;
}

RGB clearColor() { return RGB{10, 10, 10}; }

bool pixelChangedFromClear(const Framebuffer& fb, int x, int y) {
    RGB p = fb.getPixel(x, y);
    RGB c = clearColor();
    return p.r != c.r || p.g != c.g || p.b != c.b;
}
}

void test_front_facing_triangle_draws_pixels() {
    Framebuffer fb(64, 64);
    fb.clear(clearColor());
    rasterizeMesh(singleTriangle(true), defaultParams(), fb);
    TEST_ASSERT_TRUE(pixelChangedFromClear(fb, 32, 32));
}

void test_back_facing_triangle_is_culled() {
    Framebuffer fb(64, 64);
    fb.clear(clearColor());
    rasterizeMesh(singleTriangle(false), defaultParams(), fb);
    TEST_ASSERT_FALSE(pixelChangedFromClear(fb, 32, 32));
}

void test_depth_sort_picks_nearer_face_within_one_mesh() {
    Mesh m;
    // Far triangle: flat, dead-on to the camera, at local z = 0 (lambert == 1.0 exactly).
    Vec3 farA{-1, -1, 0}, farB{0, 1, 0}, farC{1, -1, 0};
    // Near triangle: same footprint, pulled toward the camera and tilted (non-uniform
    // local z among its 3 vertices), so its normal isn't parallel to the light and its
    // shading is measurably dimmer than the far triangle's.
    Vec3 nearA{-1, -1, -1.2f}, nearB{0, 1, -0.6f}, nearC{1, -1, -1.2f};

    m.vertices = { farA, farB, farC, nearA, nearB, nearC };
    m.faces = { {0, 1, 2}, {3, 4, 5} };

    Framebuffer fb(64, 64);
    fb.clear(clearColor());
    rasterizeMesh(m, defaultParams(), fb);

    RGB center = fb.getPixel(32, 32);
    // A dead-on-lit far face alone would read exactly baseColor.r (200). If the nearer,
    // tilted face correctly wins the depth sort, the visible pixel is dimmer than that.
    TEST_ASSERT_TRUE(center.r < 200);
    TEST_ASSERT_TRUE(center.r > 0);
}

void test_directly_lit_face_is_brighter_than_grazing_face() {
    RasterParams litParams = defaultParams();
    litParams.lightDir = Vec3{0, 0, -1}.normalized();

    RasterParams grazingParams = defaultParams();
    grazingParams.lightDir = Vec3{1, 0, 0}.normalized();

    Framebuffer litFb(64, 64);
    litFb.clear(clearColor());
    rasterizeMesh(singleTriangle(true), litParams, litFb);

    Framebuffer grazingFb(64, 64);
    grazingFb.clear(clearColor());
    rasterizeMesh(singleTriangle(true), grazingParams, grazingFb);

    TEST_ASSERT_TRUE(litFb.getPixel(32, 32).r > grazingFb.getPixel(32, 32).r);
}

void test_get_pixel_out_of_range_returns_black_instead_of_crashing() {
    Framebuffer fb(64, 64);
    fb.clear(clearColor());

    RGB negX = fb.getPixel(-1, 10);
    RGB negY = fb.getPixel(10, -1);
    RGB tooWideX = fb.getPixel(64, 10);
    RGB tooTallY = fb.getPixel(10, 64);

    TEST_ASSERT_EQUAL_UINT8(0, negX.r);
    TEST_ASSERT_EQUAL_UINT8(0, negX.g);
    TEST_ASSERT_EQUAL_UINT8(0, negX.b);
    TEST_ASSERT_EQUAL_UINT8(0, negY.r);
    TEST_ASSERT_EQUAL_UINT8(0, tooWideX.r);
    TEST_ASSERT_EQUAL_UINT8(0, tooTallY.r);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_front_facing_triangle_draws_pixels);
    RUN_TEST(test_back_facing_triangle_is_culled);
    RUN_TEST(test_depth_sort_picks_nearer_face_within_one_mesh);
    RUN_TEST(test_directly_lit_face_is_brighter_than_grazing_face);
    RUN_TEST(test_get_pixel_out_of_range_returns_black_instead_of_crashing);
    return UNITY_END();
}
