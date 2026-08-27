#pragma once
#include <cstdint>

// Qi earned while the device was off, based on elapsed RTC time. Negative elapsed time
// (clock moved backward / RTC unset) clamps to zero; elapsed time is capped at
// maxOfflineSeconds to avoid runaway first-boot numbers.
double computeOfflineEarnings(int64_t rtcNowEpochSeconds, int64_t lastSaveEpochSeconds,
                               double qiPerSecondAtSave, int64_t maxOfflineSeconds);
