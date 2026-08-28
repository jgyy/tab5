#include "zone_textures.h"
#include "hash.h"
#include "realms.h"
#include <cstdint>
#include <cmath>

namespace {
constexpr float kDegreesPerRealm = 360.0f / 16.0f;

uint8_t toByte(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < 0.0f) v = 0.0f;
    return static_cast<uint8_t>(v * 255.0f);
}

// Standard HSV -> RGB conversion; h in degrees (any range, wrapped), s and v in [0,1].
RGB hsvToRgb(float h, float s, float v) {
    float c = v * s;
    float hp = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f) / 60.0f;
    float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r1, g1, b1;
    if      (hp < 1.0f) { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2.0f) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3.0f) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4.0f) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5.0f) { r1 = x; g1 = 0; b1 = c; }
    else                { r1 = c; g1 = 0; b1 = x; }
    float m = v - c;
    return RGB{toByte(r1 + m), toByte(g1 + m), toByte(b1 + m)};
}

float realmHue(int realmIndex) {
    return static_cast<float>(realmIndex) * kDegreesPerRealm;
}
} // namespace

RGB zoneSkyColor(int realmIndex) {
    return hsvToRgb(realmHue(realmIndex), 0.35f, 0.85f);
}

RGB zoneGroundColor(int realmIndex) {
    return hsvToRgb(realmHue(realmIndex), 0.55f, 0.45f);
}

RGB platformColor(int realmIndex) {
    return hsvToRgb(realmHue(realmIndex), 0.45f, 0.65f);
}

RGB monsterColor(int realmIndex, int tierIndex) {
    float t = static_cast<float>(tierIndex) / 2.0f; // 0, 0.5, 1.0 for tiers 0,1,2
    float hueJitter = (hashUnitFloat(realmIndex, tierIndex) - 0.5f) * 20.0f; // +-10 degrees
    float hue = realmHue(realmIndex) + 180.0f + hueJitter; // opposite the background hue
    float value = 0.85f - 0.35f * t;    // darkens with tier
    float saturation = 0.6f + 0.3f * t; // more saturated (angrier) with tier
    return hsvToRgb(hue, saturation, value);
}

RGB characterAuraColor(int realmIndex) {
    float t = static_cast<float>(realmIndex) / static_cast<float>(NUM_REALMS - 1);
    float saturation = 0.5f + 0.3f * t; // climbs from 0.5 at realm 0 to 0.8 at realm 15
    return hsvToRgb(realmHue(realmIndex), saturation, 0.9f);
}
