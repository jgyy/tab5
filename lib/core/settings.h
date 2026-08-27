#pragma once
#include <cstdint>

// Brightness is never allowed down to 0: an unreadable black screen has no way to see the
// "+" button to recover from it. Volume has no such footgun - 0 (mute) is a normal setting.
constexpr uint8_t kMinBrightness = 20;
constexpr uint8_t kMaxBrightness = 255;
constexpr uint8_t kMinVolume = 0;
constexpr uint8_t kMaxVolume = 255;
constexpr int kSettingsStep = 32;

// Clamps `value` into [kMinBrightness, kMaxBrightness]. Takes a plain int (not uint8_t) so
// callers compute "current + step" or "current - step" in int arithmetic first - doing that
// math directly in uint8_t would silently wrap around (e.g. 240 + 32 -> 16) instead of
// clamping at the ceiling.
uint8_t clampBrightness(int value);

// Same idea as clampBrightness, for volume's [kMinVolume, kMaxVolume] range.
uint8_t clampVolume(int value);
