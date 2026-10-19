#include "carusos_config.h"

#if CARUSOS_USE_RTC
#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "SensorPCF85063.hpp"  // from SensorLib
#include "rtc_pcf85063.h"

static SensorPCF85063 rtc;
static bool rtc_ready = false;

// Diagnostics for "not found": tell "nothing answers at 0x51" (wrong
// address, chip not fitted, bus problem) apart from "something answers but
// SensorLib's identity check fails" (e.g. a PCF8563 instead of a PCF85063).
static void rtc_log_bus() {
    Wire.beginTransmission(PCF85063_SLAVE_ADDRESS);
    uint8_t err = Wire.endTransmission();
    Serial.printf("[RTC] Probe 0x%02X: %s (err %u)\n", PCF85063_SLAVE_ADDRESS,
                  err == 0 ? "ACK" : "no ACK", err);
    Serial.print("[RTC] I2C devices on the shared bus:");
    for (uint8_t a = 1; a < 0x7F; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
    }
    Serial.println();
}

bool rtc_begin() {
    if (rtc_ready) return true;
    // Wire is already started by lvgl_port_init(); pass no pins so SensorLib
    // doesn't try to begin() the shared bus a second time.
    // A few tries: the shared bus can hiccup at boot (see backend_core.cpp).
    bool found = false;
    for (int i = 0; i < 3 && !found; i++) {
        if (i) delay(100);
        found = rtc.begin(Wire);
    }
    if (!found) {
        Serial.println("[RTC] PCF85063 not found!");
        rtc_log_bus();
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

bool rtc_set(const struct tm &t) {
    if (!rtc_ready) return false;
    rtc.setDateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec);
    return true;
}
#endif // CARUSOS_USE_RTC
