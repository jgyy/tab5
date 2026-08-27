#include "offline_earnings.h"

double computeOfflineEarnings(int64_t rtcNowEpochSeconds, int64_t lastSaveEpochSeconds,
                               double qiPerSecondAtSave, int64_t maxOfflineSeconds) {
    int64_t elapsed = rtcNowEpochSeconds - lastSaveEpochSeconds;
    if (elapsed < 0) elapsed = 0;
    if (elapsed > maxOfflineSeconds) elapsed = maxOfflineSeconds;
    return static_cast<double>(elapsed) * qiPerSecondAtSave;
}
