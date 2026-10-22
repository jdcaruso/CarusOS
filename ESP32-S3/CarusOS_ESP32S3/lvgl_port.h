#pragma once
#include <stdint.h>

void lvgl_port_init();

// Hardware control
void lvgl_port_set_backlight(bool state);
void lvgl_port_set_brightness(uint8_t pct); // 10..100 %, applied now if lit
void lvgl_port_set_audio_amp(bool state);
void lvgl_port_prepare_deep_sleep(); // backlight off (held through sleep), amp off
void lvgl_port_flush();
