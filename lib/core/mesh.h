#pragma once
#include <vector>
#include "math3d.h"
#include "color.h"
#include "realms.h"

struct Face { int a, b, c; };

struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<Face> faces;
};

Mesh makeIcosahedron();
Mesh subdivide(const Mesh& mesh); // one level of subdivision; new vertices land on the unit sphere

struct RealmVisual {
    Mesh mesh;
    RGB baseColor;
    RGB rimColor;
};

// Deterministic per-(vertex,realm) pseudo-random value in [0,1) — no RNG state, so the
// same inputs always produce the same output (see growForRealm's determinism test).
float hashJaggedness(int vertexIndex, int realmIndex);

// Displaces `base`'s vertices outward along their own direction from the mesh center by
// an amount that grows with realmIndex, and returns that mesh plus the realm's base/rim
// color palette. realmIndex is clamped into [0, NUM_REALMS - 1].
RealmVisual growForRealm(const Mesh& base, int realmIndex);
