#include "rasterizer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 ab = b - a;
    Vec3 ac = c - a;
    return ab.cross(ac).normalized();
}

RGB shade(const Vec3& normal, const RasterParams& params) {
    float lambert = normal.dot(params.lightDir);
    if (lambert < 0.0f) lambert = 0.0f;
    float rim = 1.0f - normal.dot(params.viewDir);
    if (rim < 0.0f) rim = 0.0f;
    rim = rim * rim * rim; // sharpen the falloff (cheap fake-fresnel "aura" glow)

    auto mix = [](uint8_t base, uint8_t rimC, float lambertT, float rimT) -> uint8_t {
        float intensity = 0.25f + 0.75f * lambertT; // ambient floor so unlit faces aren't pure black
        float v = base * intensity + rimC * rimT;
        if (v > 255.0f) v = 255.0f;
        return static_cast<uint8_t>(v);
    };

    return RGB{
        mix(params.baseColor.r, params.rimColor.r, lambert, rim),
        mix(params.baseColor.g, params.rimColor.g, lambert, rim),
        mix(params.baseColor.b, params.rimColor.b, lambert, rim)
    };
}

void fillTriangle(Framebuffer& fb, float x0, float y0, float x1, float y1,
                   float x2, float y2, RGB color) {
    int minX = static_cast<int>(std::floor(std::min({x0, x1, x2})));
    int maxX = static_cast<int>(std::ceil(std::max({x0, x1, x2})));
    int minY = static_cast<int>(std::floor(std::min({y0, y1, y2})));
    int maxY = static_cast<int>(std::ceil(std::max({y0, y1, y2})));
    minX = std::max(minX, 0); minY = std::max(minY, 0);
    maxX = std::min(maxX, fb.width - 1); maxY = std::min(maxY, fb.height - 1);

    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-6f) return;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = static_cast<float>(x) + 0.5f;
            float py = static_cast<float>(y) + 0.5f;
            float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
            float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
            float w2 = 1.0f - w0 - w1;
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                fb.setPixel(x, y, color);
            }
        }
    }
}

} // namespace

void rasterizeMesh(const Mesh& mesh, const RasterParams& params, Framebuffer& fb) {
    std::vector<Vec3> camSpace(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vec3 rotated = params.transform.transformPoint(mesh.vertices[i]);
        camSpace[i] = { rotated.x, rotated.y, rotated.z + params.cameraDistance };
    }

    struct DrawFace { int faceIndex; float avgZ; };
    std::vector<DrawFace> visible;
    visible.reserve(mesh.faces.size());

    for (size_t i = 0; i < mesh.faces.size(); ++i) {
        const Face& f = mesh.faces[i];
        const Vec3& a = camSpace[f.a];
        const Vec3& b = camSpace[f.b];
        const Vec3& c = camSpace[f.c];
        Vec3 normal = faceNormal(a, b, c);
        // Camera sits at the origin looking down +Z, so "toward camera" from a point p
        // is -p (normalized). Cull if the face points away from the camera.
        Vec3 towardCamera = (a * -1.0f).normalized();
        if (normal.dot(towardCamera) <= 0.0f) continue;

        float avgZ = (a.z + b.z + c.z) / 3.0f;
        visible.push_back({static_cast<int>(i), avgZ});
    }

    std::sort(visible.begin(), visible.end(), [](const DrawFace& lhs, const DrawFace& rhs) {
        return lhs.avgZ > rhs.avgZ; // farthest (largest Z) first -> painter's algorithm
    });

    for (const auto& df : visible) {
        const Face& f = mesh.faces[df.faceIndex];
        const Vec3& a = camSpace[f.a];
        const Vec3& b = camSpace[f.b];
        const Vec3& c = camSpace[f.c];
        RGB color = shade(faceNormal(a, b, c), params);

        ProjectedPoint pa = project(a, params.focalLength, fb.width, fb.height);
        ProjectedPoint pb = project(b, params.focalLength, fb.width, fb.height);
        ProjectedPoint pc = project(c, params.focalLength, fb.width, fb.height);
        fillTriangle(fb, pa.screenX, pa.screenY, pb.screenX, pb.screenY, pc.screenX, pc.screenY, color);
    }
}
