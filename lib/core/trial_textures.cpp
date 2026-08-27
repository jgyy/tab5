#include "trial_textures.h"
#include <cstdint>

namespace {
uint8_t scaleChannel(uint8_t c, float factor) {
    float v = static_cast<float>(c) * factor;
    if (v > 255.0f) v = 255.0f;
    if (v < 0.0f) v = 0.0f;
    return static_cast<uint8_t>(v);
}

// Deterministic pseudo-random value in [0,1) from integer texel coordinates - same technique
// as mesh.cpp's hashJaggedness, no RNG state.
float hashTexel(int tx, int ty, int salt) {
    uint32_t h = static_cast<uint32_t>(tx) * 374761393u + static_cast<uint32_t>(ty) * 668265263u +
                 static_cast<uint32_t>(salt) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000);
}
} // namespace

RGB sampleWallTexture(int wallType, float u, float v, RGB baseColor) {
    int tx = static_cast<int>(u * kWallTextureSize) % kWallTextureSize;
    int ty = static_cast<int>(v * kWallTextureSize) % kWallTextureSize;
    if (tx < 0) tx += kWallTextureSize;
    if (ty < 0) ty += kWallTextureSize;

    float factor = 1.0f;
    if (wallType == 1) {
        // Brick-like grid: darker "mortar" lines every 8 texels.
        bool mortarLine = (tx % 8 == 0) || (ty % 8 == 0);
        factor = mortarLine ? 0.6f : 0.95f + 0.1f * hashTexel(tx, ty, wallType);
    } else {
        // Marbled vein pattern for inner/other wall types.
        float n = hashTexel(tx / 3, ty / 3, wallType);
        factor = 0.7f + 0.4f * n;
    }

    return RGB{
        scaleChannel(baseColor.r, factor),
        scaleChannel(baseColor.g, factor),
        scaleChannel(baseColor.b, factor),
    };
}
