#pragma once

#include <Arduino.h>

// Initializes the backend tasks (Core 0)
void backend_core_init();

// Thread-safe query for WiFi status
bool backend_is_wifi_connected();

// Thread-safe query for NTP Time
bool backend_get_time(int &hour, int &minute);

// Audio control
void backend_set_audio_state(bool play);
void backend_set_audio_volume(int volume);
int backend_get_audio_volume();

// Microphone test (ES7210). Start/stop capture and read a 0-100 input level.
void backend_mic_start();
void backend_mic_stop();
int backend_get_mic_level();

// System Stats
void backend_get_sys_info(uint32_t &free_heap, uint32_t &total_heap, 
                          uint32_t &free_psram, uint32_t &total_psram,
                          uint32_t &sketch_size, uint32_t &sketch_free,
                          uint32_t &flash_size);

// OTA & Network
void backend_enable_ota();
String backend_get_ip();
void backend_log(const char *msg);

// Settings
bool backend_get_telnet();
void backend_set_telnet(bool enable);
bool backend_get_animations();
void backend_set_animations(bool enable);
bool backend_get_bluetooth();
void backend_set_bluetooth(bool enable);

// Network Utils
void* backend_download_file(const char* url, size_t* out_size);
void backend_wifi_reconnect();

// File System
String backend_list_files();
