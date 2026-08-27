#pragma once
#include <cstdint>

// Returns the Tab5's RX8130CE RTC time as Unix epoch seconds, or 0 if it can't be read.
// Only the *elapsed* difference between two calls to this function matters for offline
// earnings — the RTC's absolute wall-clock accuracy is irrelevant (no NTP sync in v1).
int64_t readRtcEpochSeconds();

void writeRtcFromEpochSeconds(int64_t epoch);
