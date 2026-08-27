#include "mesh.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

Mesh makeIcosahedron() {
    float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    Mesh mesh;
    mesh.vertices = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
    };
    for (auto& v : mesh.vertices) v = v.normalized();

    mesh.faces = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
        {3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1}
    };
    return mesh;
}

namespace {
struct EdgeKey {
    int lo, hi;
    bool operator==(const EdgeKey& o) const { return lo == o.lo && hi == o.hi; }
};
struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const {
        return static_cast<size_t>(k.lo) * 100003u + static_cast<size_t>(k.hi);
    }
};
}

Mesh subdivide(const Mesh& mesh) {
    Mesh out;
    out.vertices = mesh.vertices;
    std::unordered_map<EdgeKey, int, EdgeKeyHash> midpointCache;

    auto midpoint = [&](int i0, int i1) -> int {
        EdgeKey key{std::min(i0, i1), std::max(i0, i1)};
        auto found = midpointCache.find(key);
        if (found != midpointCache.end()) return found->second;
        Vec3 mid = (out.vertices[i0] + out.vertices[i1]) * 0.5f;
        mid = mid.normalized();
        out.vertices.push_back(mid);
        int newIndex = static_cast<int>(out.vertices.size()) - 1;
        midpointCache[key] = newIndex;
        return newIndex;
    };

    for (const auto& f : mesh.faces) {
        int ab = midpoint(f.a, f.b);
        int bc = midpoint(f.b, f.c);
        int ca = midpoint(f.c, f.a);
        out.faces.push_back({f.a, ab, ca});
        out.faces.push_back({f.b, bc, ab});
        out.faces.push_back({f.c, ca, bc});
        out.faces.push_back({ab, bc, ca});
    }
    return out;
}

float hashJaggedness(int vertexIndex, int realmIndex) {
    uint32_t h = static_cast<uint32_t>(vertexIndex) * 374761393u
                + static_cast<uint32_t>(realmIndex) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h % 10000) / 10000.0f;
}

namespace {
// Base color, then rim (glow) color, per realm — pale/clear through radiant white-gold,
// following the xianxia realm-color progression from the design spec.
const RGB kBaseColors[NUM_REALMS] = {
    {220, 220, 220}, { 90, 160, 240}, { 90, 220, 140}, {230, 190,  70},
    {170, 100, 230}, {210,  60,  60}, {255, 245, 210},
};
const RGB kRimColors[NUM_REALMS] = {
    {255, 255, 255}, {140, 200, 255}, {160, 255, 200}, {255, 230, 140},
    {220, 170, 255}, {255, 140, 140}, {255, 255, 255},
};
}

RealmVisual growForRealm(const Mesh& base, int realmIndex) {
    int clampedRealm = realmIndex;
    if (clampedRealm < 0) clampedRealm = 0;
    if (clampedRealm >= NUM_REALMS) clampedRealm = NUM_REALMS - 1;

    RealmVisual result;
    result.mesh = base;
    float jaggedness = 0.05f + 0.05f * static_cast<float>(clampedRealm);
    const float kMaxDisplacement = 0.5f;

    for (size_t i = 0; i < result.mesh.vertices.size(); ++i) {
        Vec3 dir = base.vertices[i].normalized();
        float offset = hashJaggedness(static_cast<int>(i), clampedRealm) * jaggedness;
        if (offset > kMaxDisplacement) offset = kMaxDisplacement;
        result.mesh.vertices[i] = base.vertices[i] + dir * offset;
    }

    result.baseColor = kBaseColors[clampedRealm];
    result.rimColor = kRimColors[clampedRealm];
    return result;
}
