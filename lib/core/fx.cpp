#include "fx.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

float shakeOffset(float t, float amplitudePx, float phaseRadians) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    // sin(t*pi), not a linear (1-t) decay: the linear form is only 0 at t=1, so at t=0 it
    // reduces to amplitudePx*sin(phaseRadians) - the FULL amplitude when phaseRadians=pi/2,
    // exactly the phase renderZoneView() uses for the Y shake axis. sin(t*pi) is 0 at both
    // t=0 and t=1 for any phaseRadians, matching this function's documented contract.
    float envelope = std::sin(t * kPi);
    return amplitudePx * envelope * std::sin(t * 6.0f * kPi + phaseRadians);
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

float pulseEnvelope(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return std::sin(t * kPi);
}
