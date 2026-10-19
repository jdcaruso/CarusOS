#pragma once

// Audio codec pins (ES7210 mic ADC / ES8311 speaker DAC)
#define PIN_ES7210_BCLK       16
#define PIN_ES7210_LRCK       7
#define PIN_ES7210_DIN        15
#define PIN_ES7210_MCLK       5
#define PIN_ES8311_DOUT       6

// Shared I2C bus (GT911 touch, PCF85063 RTC, QMI8658 IMU, XCA9554 expander)
#define PIN_I2C_SDA           47
#define PIN_I2C_SCL           48

// LCD backlight: GPIO4 drives the CTRL pin of the AP3032 LED driver
// (PWM-capable). From the board schematic (net "BL"). Active LOW: low = lit,
// high = dark (measured on the board).
#define PIN_LCD_BL            4

// XCA9554 I/O-expander channels (NOT GPIOs), from the board schematic.
// EXIO0-2 (panel SPI) and EXIO7 (panel reset) are driven by Arduino_GFX's
// Arduino_XCA9554SWSPI in lvgl_port.cpp. EXIO4 reads the PWR key.
#define EXP_PIN_AMP           3 // ES8311 amplifier enable (PA_CTRL)
#define EXP_PIN_TP_RST        5 // GT911 touch reset
#define EXP_PIN_TP_INT        6 // GT911 INT (held low during reset = I2C 0x5D)
