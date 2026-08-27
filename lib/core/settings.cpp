#include "settings.h"

namespace {
uint8_t clampToRange(int value, uint8_t lo, uint8_t hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return static_cast<uint8_t>(value);
}
} // namespace

uint8_t clampBrightness(int value) {
    return clampToRange(value, kMinBrightness, kMaxBrightness);
}

uint8_t clampVolume(int value) {
    return clampToRange(value, kMinVolume, kMaxVolume);
}
