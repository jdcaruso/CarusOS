#pragma once
#include "carusos_config.h"
#include <time.h>

// PCF85063 battery-backed RTC over I2C (shared Wire bus, pins 47/48).
// Keeps the clock alive without WiFi/NTP and across reboots.
// Uses SensorLib's SensorPCF85063.

#if CARUSOS_USE_RTC
bool rtc_begin();                 // init the RTC; true if found
bool rtc_get(struct tm &out);     // read RTC into a struct tm
void rtc_set(const struct tm &t); // write a struct tm into the RTC
#endif
