#include "carusos_config.h"

#if CARUSOS_USE_RTC
#include <Arduino.h>
#include <Wire.h>
#include "SensorPCF85063.hpp"  // from SensorLib
#include "rtc_pcf85063.h"

static SensorPCF85063 rtc;
static bool rtc_ready = false;

bool rtc_begin() {
    if (rtc_ready) return true;
    if (!rtc.begin(Wire, 47, 48)) {
        Serial.println("[RTC] PCF85063 not found!");
        return false;
    }
    rtc_ready = true;
    Serial.println("[RTC] PCF85063 ready.");
    return true;
}

bool rtc_get(struct tm &out) {
    if (!rtc_ready) return false;
    RTC_DateTime dt = rtc.getDateTime();
    out.tm_year  = dt.getYear() - 1900;
    out.tm_mon   = dt.getMonth() - 1;
    out.tm_mday  = dt.getDay();
    out.tm_hour  = dt.getHour();
    out.tm_min   = dt.getMinute();
    out.tm_sec   = dt.getSecond();
    out.tm_isdst = -1;
    return true;
}

void rtc_set(const struct tm &t) {
    if (!rtc_ready) return;
    rtc.setDateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec);
}
#endif // CARUSOS_USE_RTC
