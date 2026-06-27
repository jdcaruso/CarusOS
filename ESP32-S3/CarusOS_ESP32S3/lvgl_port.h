#pragma once

void lvgl_port_init();

// Hardware control
void lvgl_port_set_backlight(bool state);
void lvgl_port_set_audio_amp(bool state);
void lvgl_port_flush();
