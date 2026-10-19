#include "carusos_config.h"

#if CARUSOS_USE_PMU
#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "pmu_axp2101.h"

static XPowersPMU pmu;
static bool pmu_ready = false;

bool pmu_begin() {
    if (pmu_ready) return true;
    // Wire is already started by lvgl_port_init(); XPowersLib calls
    // Wire.begin(sda, scl) again, which is a no-op on a running bus.
    if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL)) {
        Serial.println("[PMU] AXP2101 not found!");
        return false;
    }
    // Only the measurement ADCs: no rail, charger or IRQ settings are changed.
    pmu.enableBattDetection();
    pmu.enableVbusVoltageMeasure();
    pmu.enableBattVoltageMeasure();
    pmu.enableSystemVoltageMeasure();
    pmu_ready = true;
    Serial.println("[PMU] AXP2101 ready.");
    return true;
}

bool pmu_read(pmu_status_t &out) {
    if (!pmu_ready) return false;
    out.battery  = pmu.isBatteryConnect();
    out.charging = out.battery && pmu.isCharging();
    out.usb      = pmu.isVbusIn();
    out.percent  = out.battery ? pmu.getBatteryPercent() : -1;
    out.batt_mv  = out.battery ? pmu.getBattVoltage() : 0;
    return true;
}
#endif // CARUSOS_USE_PMU
