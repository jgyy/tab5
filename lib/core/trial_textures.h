#pragma once
#include "color.h"

constexpr int kWallTextureSize = 32; // logical texel grid per wall face, sampled procedurally

// Procedurally shades a wall texel at fractional (u, v) in [0,1) for the given wall type,
// tinting `baseColor` with a deterministic brick/vein-style pattern (a grid of darker mortar
// lines for wallType 1, a marbled hash-based vein pattern for wallType 2 and above). Same
// (wallType, u, v, baseColor) always produces the same result - no RNG.
RGB sampleWallTexture(int wallType, float u, float v, RGB baseColor);
