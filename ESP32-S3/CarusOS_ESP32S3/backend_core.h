#pragma once

#include <Arduino.h>

// Initializes the backend tasks (Core 0)
void backend_core_init();

// Thread-safe query for WiFi status
bool backend_is_wifi_connected();

// Full date+time (system clock: NTP-synced or RTC-seeded). True if valid.
bool backend_get_datetime(struct tm &out);

// Power (AXP2101, polled on Core 0). False if the PMU is off or not found;
// percent is -1 without a battery.
bool backend_get_power(int &percent, bool &charging, bool &usb, int &batt_mv);

// Audio control
void backend_set_audio_state(bool play);
void backend_set_audio_volume(int volume);
int backend_get_audio_volume();
// Short soft tone at `volume` (0-100), played by the audio task, then the user
// volume is restored. Ignored while a tone plays or after the Mic Test.
// Only does something with CARUSOS_DASHBOARD_ALERT.
void backend_beep(int volume, int duration_ms);

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
bool backend_get_wifi_enabled();
void backend_set_wifi_enabled(bool enable);
bool backend_get_dash_alert();          // Dashboard pre-event beep (runtime, NVS)
void backend_set_dash_alert(bool enable);

// Network Utils
void* backend_download_file(const char* url, size_t* out_size);
void backend_wifi_reconnect();

// File System (async: work runs on Core 0, UI polls the text result on Core 1)
void backend_fs_request_list();          // ask Core 0 to list files
void backend_fs_request_format();        // ask Core 0 to format/mount (never at boot)
bool backend_fs_poll(char *out, size_t out_size); // true + copies text when ready
