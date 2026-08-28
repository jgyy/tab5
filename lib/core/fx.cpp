#include "fx.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

float shakeOffset(float t, float amplitudePx, float phaseRadians) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float decay = 1.0f - t;
    return amplitudePx * decay * std::sin(t * 6.0f * kPi + phaseRadians);
}

float damageNumberRiseOffsetPx(float elapsedSeconds, float risePxPerSecond) {
    if (elapsedSeconds < 0.0f) elapsedSeconds = 0.0f;
    return -(elapsedSeconds * risePxPerSecond);
}

float parallaxWrapX(float seedX, float pxPerSecond, float elapsedSeconds, float viewportW) {
    if (viewportW <= 0.0f) return seedX;
    float raw = seedX + pxPerSecond * elapsedSeconds;
    float wrapped = std::fmod(raw, viewportW);
    if (wrapped < 0.0f) wrapped += viewportW;
    return wrapped;
}
