#pragma once

// Power management chip (used by XPowersLib on the Waveshare ESP32-S3 board)
#define XPOWERS_CHIP_AXP2101

// Audio codec pins (ES7210 mic ADC / ES8311 speaker DAC)
#define PIN_ES7210_BCLK       16
#define PIN_ES7210_LRCK       7
#define PIN_ES7210_DIN        15
#define PIN_ES7210_MCLK       5
#define PIN_ES8311_DOUT       6

// Shared I2C bus (GT911 touch, PCF85063 RTC, QMI8658 IMU, XCA9554 expander)
#define PIN_I2C_SDA           47
#define PIN_I2C_SCL           48

// XCA9554 I/O-expander output channels (NOT GPIOs)
#define EXP_PIN_AMP           3 // ES8311 amplifier enable
#define EXP_PIN_BACKLIGHT     5 // LCD backlight enable
#define EXP_PIN_PANEL_RST     6 // ST7701 panel reset
