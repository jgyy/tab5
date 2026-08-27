#include "rtc_store.h"
#include <M5Unified.h>
#include <ctime>

// Verified against the actual fetched M5Unified source
// (.pio/libdeps/esp32p4_pioarduino/M5Unified/src/utility/rtc/RTC_Base.hpp and
// .../utility/RTC_Class.hpp) rather than assumed. Confirmed for the Tab5/Tab5X boards
// specifically: RTC_Class::begin() (utility/RTC_Class.cpp) instantiates RX8130_Class for
// board_t::board_M5Tab5 / board_M5Tab5X on ESP32P4, so M5.Rtc really does drive the
// RX8130CE on this hardware (not the PCF8563/BM8563 other M5Stack boards use).
//
// Struct/method shape found (all in namespace m5):
//   struct rtc_time_t { int8_t hours, minutes, seconds; };
//   struct rtc_date_t { int16_t year; int8_t month, date, weekDay; };
//   struct rtc_datetime_t { rtc_date_t date; rtc_time_t time; };
//   bool RTC_Class::getDateTime(rtc_datetime_t* datetime) const;
//   void RTC_Class::setDateTime(const rtc_datetime_t& datetime);
// This matches the brief's template field-for-field and call-for-call, so no adaptation
// was needed.

namespace {
// Days since 1970-01-01 for civil (proleptic Gregorian) date y/m/d, UTC, no DST.
// This is the well-known Howard Hinnant "days_from_civil" algorithm; it's a drop-in
// substitute for timegm()'s date arithmetic. Needed because this toolchain's actual
// newlib time.h (riscv32-esp-elf/include/time.h, confirmed by inspecting the compiler's
// real #include <...> search path) declares gmtime_r/mktime but not timegm — unlike the
// brief's template, which assumed timegm() was available.
int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);            // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
}  // namespace

int64_t readRtcEpochSeconds() {
    m5::rtc_datetime_t dt;
    if (!M5.Rtc.getDateTime(&dt)) return 0;

    int64_t days = daysFromCivil(dt.date.year, dt.date.month, dt.date.date);
    return days * 86400 + dt.time.hours * 3600 + dt.time.minutes * 60 + dt.time.seconds;
}

void writeRtcFromEpochSeconds(int64_t epoch) {
    time_t t = static_cast<time_t>(epoch);
    struct tm timeinfo;
    gmtime_r(&t, &timeinfo);

    m5::rtc_datetime_t dt;
    dt.date.year  = timeinfo.tm_year + 1900;
    dt.date.month = timeinfo.tm_mon + 1;
    dt.date.date  = timeinfo.tm_mday;
    dt.time.hours   = timeinfo.tm_hour;
    dt.time.minutes = timeinfo.tm_min;
    dt.time.seconds = timeinfo.tm_sec;

    M5.Rtc.setDateTime(dt);
}
