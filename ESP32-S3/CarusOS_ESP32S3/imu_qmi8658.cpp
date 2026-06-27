#include "carusos_config.h"

#if ENABLE_APP_IMU
#include <Arduino.h>
#include <Wire.h>
#include "SensorQMI8658.hpp"  // from SensorLib (already used for the GT911 touch)
#include "imu_qmi8658.h"

static SensorQMI8658 qmi;
static bool imu_ready = false;
static bool gyro_on = false;

bool imu_begin() {
    if (imu_ready) return true;
    // Shared I2C bus on pins 47 (SDA) / 48 (SCL), already started by lvgl_port.
    if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, 47, 48)) {
        Serial.println("[IMU] QMI8658 not found!");
        return false;
    }
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_1000Hz,
                            SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();

    qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                        SensorQMI8658::GYR_ODR_896_8Hz,
                        SensorQMI8658::LPF_MODE_3);
    qmi.enableGyroscope();
    gyro_on = true;

    imu_ready = true;
    Serial.println("[IMU] QMI8658 ready.");
    return true;
}

bool imu_get_accel(float &x, float &y, float &z) {
    if (!imu_ready) return false;
    if (!qmi.getDataReady()) return false;
    return qmi.getAccelerometer(x, y, z);
}

bool imu_get_gyro(float &x, float &y, float &z) {
    if (!imu_ready || !gyro_on) return false;
    return qmi.getGyroscope(x, y, z);
}
#endif // ENABLE_APP_IMU
