#pragma once
#include "carusos_config.h"

// AXP2101 power-management IC over I2C (shared Wire bus, pins 47/48, 0x34).
// Read-only use: battery / USB / charge status. CarusOS never writes the
// power rails or the charger settings (DCDC1 powers the ESP32 itself).
// Uses XPowersLib. Call from Core 0 only (Task_Backend).

#if CARUSOS_USE_PMU
typedef struct {
    bool battery;    // a battery is connected
    bool charging;   // charger is active
    bool usb;        // VBUS (USB power) present
    int  percent;    // battery %, -1 if unknown / no battery
    int  batt_mv;    // battery voltage in mV, 0 if no battery
} pmu_status_t;

bool pmu_begin();                  // init the PMU and its ADCs; true if found
bool pmu_read(pmu_status_t &out);  // false if the PMU isn't ready
#endif
