#pragma once
#include "carusos_config.h"

// QMI8658 6-axis IMU (accelerometer + gyroscope) over I2C (shared Wire bus,
// pins 47/48 - same bus as the touch panel). Uses SensorLib's SensorQMI8658.

#if ENABLE_APP_IMU
bool imu_begin();                                  // init the IMU; true if found
bool imu_get_accel(float &x, float &y, float &z);  // acceleration in g
bool imu_get_gyro(float &x, float &y, float &z);   // angular rate in dps
#endif
