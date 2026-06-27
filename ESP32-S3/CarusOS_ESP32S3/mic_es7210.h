#pragma once
#include "carusos_config.h"

// ES7210 microphone capture (level metering).
//
// Capture is implemented with the ESP_I2S driver (the same one the speaker
// uses) on I2S_NUM_1; the ES7210 chip is configured over I2C. It lives in its
// own translation unit to keep the mic plumbing out of backend_core.cpp.

#if ENABLE_APP_MIC_TEST
void mic_capture_start();   // init ES7210 + start I2S RX task
void mic_capture_stop();    // stop task + release I2S RX
int  mic_capture_level();   // latest input level, 0-100
#endif
