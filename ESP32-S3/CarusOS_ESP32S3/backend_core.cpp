#include "backend_core.h"
#include "carusos_config.h"
#include "customizations/secrets.h" // WiFi/OTA credentials: only this file sees them
#include "pin_config.h"
#include "language.h"
#include "lvgl_port.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "rtc_pcf85063.h"
#include "pmu_axp2101.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <math.h>

#if CARUSOS_USE_OTA
#include <ArduinoOTA.h>
#endif

#include <Preferences.h>
Preferences preferences;
bool runtime_use_telnet = true;
bool runtime_use_animations = false;
bool runtime_use_bluetooth = false;
bool runtime_wifi_enabled = true;
bool runtime_dash_alert = true;

#if CARUSOS_USE_BLUETOOTH
#include <BLEDevice.h>
#include <BLEServer.h>
#endif

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#if CARUSOS_USE_TELNET_LOG
#include <WiFiServer.h>
WiFiServer telnetServer(23);
WiFiClient telnetClient;
#endif

#if CARUSOS_USE_FS
#include <FFat.h>
#include <esp_partition.h>
static volatile bool fs_mounted = false; // true only after a successful FFat mount
#endif

#if CARUSOS_USE_AUDIO
#include "ESP_I2S.h"
#include "es8311.h"

I2SClass i2s;
TaskHandle_t TaskAudio;
#endif

TaskHandle_t TaskBackend;

// Thread-safe atomic flags
static volatile bool wifi_connected = false;
static volatile bool play_audio = false;
static volatile int audio_volume = 70; // 0-100
#if CARUSOS_DASHBOARD_ALERT
// One short beep request (UI -> audio task): volume first, then the length,
// which the audio task reads and clears. Single words, one writer each side.
static volatile int beep_volume = 0;
static volatile int beep_ms = 0;
static volatile bool speaker_released = false; // the mic took the I2S pins
#endif
static volatile bool ota_enabled = false;

#if CARUSOS_USE_PMU
// Power status, written by Task_Backend every 5 s, read by the UI.
static volatile bool pmu_ok = false;
static volatile int  pmu_percent = -1;
static volatile bool pmu_charging = false;
static volatile bool pmu_usb = false;
static volatile int  pmu_batt_mv = 0;
#endif

// ---------------------------------------------------------------------------
// UI -> Backend command queue
// ---------------------------------------------------------------------------
// The UI runs on Core 1. Actions that touch WiFi/OTA/Telnet/BLE/NVS hardware
// must run on Core 0, so the UI posts commands here and Task_Backend executes
// them. (Audio play/volume use the volatile flags above, which the Core 0 audio
// task already reads directly.)
typedef enum {
    BCMD_WIFI_RECONNECT,
    BCMD_OTA_ENABLE,
    BCMD_SET_TELNET,
    BCMD_SET_ANIMATIONS,
    BCMD_SET_BLUETOOTH,
    BCMD_SET_WIFI,
    BCMD_SET_DASH_ALERT,
    BCMD_FS_LIST,     // list files (FFat runs on Core 0; result via backend_fs_poll)
    BCMD_FS_FORMAT,   // format/mount FFat on demand (slow; must not block the UI core)
} backend_cmd_type_t;

typedef struct {
    backend_cmd_type_t type;
    int arg;
} backend_cmd_t;

static QueueHandle_t backend_queue = NULL;

// Log -> Telnet queue. backend_log() can run on any core (e.g. the LVGL log
// callback on Core 1), but telnetClient must only be touched by Core 0: the
// accept loop stops/replaces the client there, so printing from another core
// races with that (use-after-free on the socket). Messages are queued here
// and drained to the client by Task_Backend on Core 0.
#if CARUSOS_USE_TELNET_LOG
typedef struct {
    char msg[160];
} log_msg_t;
static QueueHandle_t log_queue = NULL;
#endif

// File-system result buffer. The FFat work runs on Core 0 (Task_Backend) and
// leaves its text output here; the UI (Core 1) picks it up via backend_fs_poll()
// once fs_result_ready flips true. Single producer / single consumer, guarded by
// the volatile flag (same lightweight pattern as the rest of the cross-core state).
static char fs_result[512];
static volatile bool fs_result_ready = false;

// Core-0 command handlers
static void do_enable_ota();
static void do_wifi_reconnect();
static void do_set_telnet(bool enable);
static void do_set_animations(bool enable);
static void do_set_dash_alert(bool enable);
static void do_set_bluetooth(bool enable);
static void do_set_wifi(bool enable);
static void do_fs_list();
static void do_fs_format();

static void do_enable_ota() {
    if (!ota_enabled && wifi_connected) {
#if CARUSOS_USE_OTA
        ArduinoOTA.setHostname(CARUSOS_OTA_HOSTNAME);
        ArduinoOTA.setPassword(CARUSOS_OTA_PASSWORD);
        
        ArduinoOTA.onStart([]() {
            Serial.println("[OTA] Start updating");
        });
        ArduinoOTA.onEnd([]() {
            Serial.println("\n[OTA] End");
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            if (total > 0) {
                Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
            }
        });
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("[OTA] Error[%u]: ", error);
        });

        ArduinoOTA.begin();
#endif
        ota_enabled = true;
        Serial.println("[OTA] Service Started.");
    }
}

void backend_enable_ota() {
    backend_cmd_t cmd = { BCMD_OTA_ENABLE, 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

String backend_get_ip() {
    if (wifi_connected) {
        return WiFi.localIP().toString();
    }
    return TXT_DISCONNECTED;
}

// Runs on Core 0 only (called from Task_Backend). Builds the file listing.
static String fs_do_list() {
    String output = "";
#if CARUSOS_USE_FS
    // --- DEBUG instrumentation (v0.40 crash hunt). Remove once fixed. ---
    Serial.printf("[FS] list_files: core=%d stackHWM=%u fs_mounted=%d\n",
                  xPortGetCoreID(), uxTaskGetStackHighWaterMark(NULL), (int)fs_mounted);

    const esp_partition_t* fat_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
    Serial.printf("[FS] partition=%p\n", (void*)fat_part);
    if (fat_part == NULL) {
        return TXT_FS_NO_PARTITION;
    }

    // Guard: never touch FFat unless it actually mounted (avoids a null deref).
    if (!fs_mounted) {
        return TXT_FS_NOT_MOUNTED;
    }

    Serial.println("[FS] opening root...");
    File root = FFat.open("/");
    Serial.printf("[FS] root opened, valid=%d\n", (bool)root);
    if (!root) {
        return TXT_FS_ROOT_ERROR;
    }
    if (!root.isDirectory()) {
        return TXT_FS_ROOT_ERROR;
    }

    Serial.println("[FS] iterating...");
    File file = root.openNextFile();
    while (file) {
        const char * nm = file.name();
        Serial.printf("[FS] entry name=%p dir=%d\n", (const void*)nm, file.isDirectory());
        if (!file.isDirectory()) {
            output += String(nm ? nm : "?") + " - " + String(file.size() / 1024) + " KB\n";
        }
        file = root.openNextFile();
    }
    Serial.println("[FS] iteration done.");

    if (output == "") {
        output = TXT_FS_EMPTY;
    }
#else
    output = TXT_FS_DISABLED;
#endif
    return output;
}

// Runs on Core 0 only. Mounts/formats FFat, then returns the fresh listing.
static String fs_do_format() {
#if CARUSOS_USE_FS
    // Mount, formatting the partition if needed. DELIBERATE user action only -
    // never call this at boot (a slow format there can trip the watchdog).
    Serial.println("[Backend] Formatting/mounting FFat (on demand)...");
    fs_mounted = FFat.begin(true);
    Serial.printf("[Backend] FFat format result: %s\n", fs_mounted ? "OK" : "FAIL");
    if (!fs_mounted) {
        return TXT_FS_FORMAT_FAILED;
    }
    return fs_do_list();
#else
    return TXT_FS_DISABLED;
#endif
}

// --- Cross-core FS bridge (Core 0 does the work, Core 1 UI polls the result) ---

static void publish_fs_result(const String &s) {
    strncpy(fs_result, s.c_str(), sizeof(fs_result) - 1);
    fs_result[sizeof(fs_result) - 1] = '\0';
    fs_result_ready = true;
}

static void do_fs_list()   { publish_fs_result(fs_do_list()); }
static void do_fs_format() { publish_fs_result(fs_do_format()); }

// Called from the UI (Core 1): enqueue the request for Core 0 to run.
void backend_fs_request_list() {
    fs_result_ready = false;
    backend_cmd_t cmd = { BCMD_FS_LIST, 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

void backend_fs_request_format() {
    fs_result_ready = false;
    backend_cmd_t cmd = { BCMD_FS_FORMAT, 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

// Called from the UI: if a result is ready, copy it out and return true.
bool backend_fs_poll(char *out, size_t out_size) {
    if (!fs_result_ready) return false;
    strncpy(out, fs_result, out_size - 1);
    out[out_size - 1] = '\0';
    fs_result_ready = false;
    return true;
}

#if CARUSOS_USE_AUDIO
static void Task_Audio(void *pvParameters) {
    // Init I2S
    i2s.setPins(PIN_ES7210_BCLK, PIN_ES7210_LRCK, PIN_ES8311_DOUT, PIN_ES7210_DIN, PIN_ES7210_MCLK);
    if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("[Backend] I2S init failed!");
        vTaskDelete(NULL);
    }

    // Init ES8311 codec
    es8311_handle_t es_handle = es8311_create(0, ES8311_ADDRRES_0);
    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = 16000 * 256,
        .sample_frequency = 16000
    };
    es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    es8311_sample_frequency_config(es_handle, es_clk.mclk_frequency, es_clk.sample_frequency);
    es8311_microphone_config(es_handle, false);
    
    // Enable amplifier
    lvgl_port_set_audio_amp(true);

    int last_vol = -1;
    
    // Generate a simple 440Hz sine wave tone buffer (16kHz sample rate, 16-bit stereo)
    const int num_samples = 400; // 400 samples = 25ms
    int16_t *sine_buffer = (int16_t *)malloc(num_samples * 2 * sizeof(int16_t)); // *2 for stereo
    if (sine_buffer) {
        for (int i = 0; i < num_samples; i++) {
            int16_t val = (int16_t)(sin(2 * M_PI * 440.0 * i / 16000.0) * 10000);
            sine_buffer[i * 2] = val;       // Left channel
            sine_buffer[i * 2 + 1] = val;   // Right channel
        }
    }

    while (1) {
#if CARUSOS_DASHBOARD_ALERT
        if (beep_ms > 0) {
            int chunks = beep_ms / 25; // the buffer is 25 ms, a whole number of cycles
            beep_ms = 0;
            if (!play_audio && sine_buffer) {
                es8311_voice_volume_set(es_handle, beep_volume, NULL);
                for (int i = 0; i < chunks; i++)
                    i2s.write((uint8_t *)sine_buffer, num_samples * 2 * sizeof(int16_t));
                last_vol = -1; // restore the user volume below
            }
        }
#endif
        if (last_vol != audio_volume) {
            last_vol = audio_volume;
            es8311_voice_volume_set(es_handle, last_vol, NULL);
        }

        if (play_audio && sine_buffer) {
            // Write a small chunk so it doesn't block for long
            i2s.write((uint8_t *)sine_buffer, num_samples * 2 * sizeof(int16_t));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50)); // sleep when idle
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// Microphone test (ES7210)
// ---------------------------------------------------------------------------
// The capture lives in mic_es7210.cpp. Here we only handle the speaker
// hand-off: the mic and speaker share the I2S clock pins, so we release the
// speaker (i2s.end()) before the mic starts.
#include "mic_es7210.h"

void backend_mic_start() {
#if ENABLE_APP_MIC_TEST
    play_audio = false;
#if CARUSOS_DASHBOARD_ALERT
    speaker_released = true; // no beeps until reboot (see pendings.md)
#endif
#if CARUSOS_USE_AUDIO
    i2s.end(); // release the speaker I2S so the mic can own the shared clock pins
#endif
    mic_capture_start();
#endif
}

void backend_mic_stop() {
#if ENABLE_APP_MIC_TEST
    mic_capture_stop();
#endif
}

int backend_get_mic_level() {
#if ENABLE_APP_MIC_TEST
    return mic_capture_level();
#else
    return 0;
#endif
}

static void Task_Backend(void *pvParameters) {
    // Wait a bit for the system to settle
    vTaskDelay(pdMS_TO_TICKS(2000));

#if CARUSOS_USE_AUDIO
    // Start Audio Task on same Core 0
    xTaskCreatePinnedToCore(Task_Audio, "TaskAudio", 8192, NULL, 1, &TaskAudio, 0);
#endif

    // Load persisted settings (NVS) before bringing up WiFi.
    preferences.begin("carusos", false);
    runtime_use_telnet     = preferences.getBool("telnet", true);
    runtime_use_animations = preferences.getBool("anim", false);
    runtime_use_bluetooth  = preferences.getBool("bt", false);
    runtime_wifi_enabled   = preferences.getBool("wifi", true);
    runtime_dash_alert     = preferences.getBool("dashbeep", true);
    Serial.printf("[NVS] loaded: wifi=%d telnet=%d anim=%d bt=%d\n",
                  (int)runtime_wifi_enabled, (int)runtime_use_telnet,
                  (int)runtime_use_animations, (int)runtime_use_bluetooth);

#if CARUSOS_USE_RTC
    // Seed the system clock from the battery-backed RTC so the time is right
    // immediately, even before (or without) a WiFi/NTP sync. Done before
    // WiFi.begin(): probing the RTC while WiFi was starting up on this core
    // failed on some boots with the I2C bus returning garbage.
    if (rtc_begin()) {
        struct tm rtm;
        if (rtc_get(rtm) && rtm.tm_year > 120) { // valid (year > 2020)
            time_t t = mktime(&rtm);
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            Serial.println("[RTC] System time seeded from RTC.");
        }
    }
#endif

#if CARUSOS_USE_PMU
    pmu_ok = pmu_begin();
#endif

    if (runtime_wifi_enabled) {
        Serial.println("[Backend] Starting WiFi Connection...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    } else {
        Serial.println("[Backend] WiFi disabled (saved setting).");
        WiFi.mode(WIFI_OFF);
    }

#if CARUSOS_USE_FS
    Serial.println("[Backend] Checking for FAT partition...");
    const esp_partition_t* fat_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
    
    if (fat_part == NULL) {
        Serial.println("[Backend] CRITICAL: FAT Partition not found in flash!");
        Serial.println("[Backend] The OS will continue, but File System is disabled.");
        Serial.println("[Backend] To fix this, change Partition Scheme in Arduino IDE.");
    } else {
        Serial.println("[Backend] Mounting FFat...");
        // NOTE: begin(false) = DO NOT auto-format at boot. A boot-time format of
        // the large FAT partition is slow and can trip the task watchdog, which
        // caused a reboot loop before. Format must be an explicit user action.
        fs_mounted = FFat.begin(false);
        if (fs_mounted) {
            Serial.println("[Backend] FFat Mounted.");
        } else {
            Serial.println("[Backend] FFat not mounted (unformatted?). Use an explicit format action.");
        }
    }
#endif

#if CARUSOS_USE_BLUETOOTH
    if (runtime_use_bluetooth) {
        BLEDevice::init("CarusOS");
        BLEDevice::createServer();
        BLEDevice::startAdvertising();
        Serial.println("[Backend] BLE Advertising started.");
    }
#endif

#if CARUSOS_USE_TELNET_LOG
    // Telnet needs the network stack, so only start it when WiFi is enabled.
    if (runtime_use_telnet && runtime_wifi_enabled) {
        telnetServer.begin();
    }
#endif

    while (1) {
        // Drain UI commands (executed here on Core 0)
        backend_cmd_t cmd;
        while (xQueueReceive(backend_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case BCMD_WIFI_RECONNECT:  do_wifi_reconnect();             break;
                case BCMD_OTA_ENABLE:      do_enable_ota();                 break;
                case BCMD_SET_TELNET:      do_set_telnet(cmd.arg != 0);     break;
                case BCMD_SET_ANIMATIONS:  do_set_animations(cmd.arg != 0); break;
                case BCMD_SET_BLUETOOTH:   do_set_bluetooth(cmd.arg != 0);  break;
                case BCMD_SET_WIFI:        do_set_wifi(cmd.arg != 0);       break;
                case BCMD_SET_DASH_ALERT:  do_set_dash_alert(cmd.arg != 0); break;
                case BCMD_FS_LIST:         do_fs_list();                    break;
                case BCMD_FS_FORMAT:       do_fs_format();                  break;
            }
        }

        bool current_status = (WiFi.status() == WL_CONNECTED);
        
        if (current_status != wifi_connected) {
            wifi_connected = current_status;
            if (wifi_connected) {
                Serial.print("[Backend] WiFi Connected! IP: ");
                Serial.println(WiFi.localIP());

                // Sync NTP Time
#if CARUSOS_USE_NTP_TIME
                Serial.println("[Backend] Requesting NTP Time...");
                configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
#endif
            } else {
                Serial.println("[Backend] WiFi Disconnected.");
                if (runtime_wifi_enabled) WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Reconnect
            }
        }

#if CARUSOS_USE_RTC
        // Once NTP has a valid time, persist it to the RTC (once per connection).
        static bool rtc_synced_from_ntp = false;
        if (wifi_connected) {
            if (!rtc_synced_from_ntp) {
                struct tm nt;
                if (getLocalTime(&nt, 5) && nt.tm_year > 120) {
                    if (rtc_set(nt)) Serial.println("[RTC] Updated from NTP.");
                    rtc_synced_from_ntp = true; // don't retry every loop without an RTC
                }
            }
        } else {
            rtc_synced_from_ntp = false;
        }
#endif

#if CARUSOS_USE_PMU
        // Battery status changes slowly: a handful of I2C reads every 5 s.
        static uint32_t pmu_last_ms = 0;
        if (pmu_ok && (pmu_last_ms == 0 || millis() - pmu_last_ms > 5000)) {
            pmu_last_ms = millis();
            pmu_status_t ps;
            if (pmu_read(ps)) {
                static int last_state = -1; // log only when battery/USB/charging changes
                int state = (ps.battery << 2) | (ps.usb << 1) | ps.charging;
                if (state != last_state) {
                    last_state = state;
                    Serial.printf("[PMU] battery=%s %d%% %d mV, usb=%d, charging=%d\n",
                                  ps.battery ? "yes" : "no", ps.percent, ps.batt_mv,
                                  (int)ps.usb, (int)ps.charging);
                }
                pmu_percent  = ps.percent;
                pmu_charging = ps.charging;
                pmu_usb      = ps.usb;
                pmu_batt_mv  = ps.batt_mv;
            }
        }
#endif

        if (ota_enabled) {
#if CARUSOS_USE_OTA
            ArduinoOTA.handle();
#endif
            vTaskDelay(pdMS_TO_TICKS(10)); // Poll quickly for OTA
        } else {
            vTaskDelay(pdMS_TO_TICKS(50)); // Poll every 50ms normally
        }

#if CARUSOS_USE_TELNET_LOG
        if (runtime_use_telnet && runtime_wifi_enabled) {
            if (telnetServer.hasClient()) {
                if (!telnetClient || !telnetClient.connected()) {
                    if (telnetClient) telnetClient.stop();
                    telnetClient = telnetServer.available();
                    telnetClient.println("Connected to CarusOS Telnet Log!");
                } else {
                    telnetServer.available().stop();
                }
            }
        }

        // Drain queued log messages to the telnet client. Always drain (even
        // with telnet off) so the queue never fills with stale messages.
        log_msg_t lm;
        while (xQueueReceive(log_queue, &lm, 0) == pdTRUE) {
            if (runtime_use_telnet && telnetClient && telnetClient.connected()) {
                telnetClient.print(lm.msg);
            }
        }
#endif
    }
}

void backend_core_init() {
    // Queue for UI -> backend commands (executed on Core 0)
    backend_queue = xQueueCreate(10, sizeof(backend_cmd_t));

#if CARUSOS_USE_TELNET_LOG
    // Queue for log -> telnet messages (drained on Core 0)
    log_queue = xQueueCreate(16, sizeof(log_msg_t));
#endif

    // Create backend task on Core 0
    xTaskCreatePinnedToCore(
        Task_Backend,
        "TaskBackend",
        8192,
        NULL,
        1,
        &TaskBackend,
        0 // Core 0!
    );

#if CARUSOS_DASHBOARD_REMOTE
    dash_remote_start(); // its own Core 0 task: a TLS fetch must not stall this loop
#endif
}

bool backend_get_power(int &percent, bool &charging, bool &usb, int &batt_mv) {
#if CARUSOS_USE_PMU
    if (!pmu_ok) return false;
    percent  = pmu_percent;
    charging = pmu_charging;
    usb      = pmu_usb;
    batt_mv  = pmu_batt_mv;
    return true;
#else
    return false;
#endif
}

bool backend_is_wifi_connected() {
    return wifi_connected;
}

bool backend_get_datetime(struct tm &out) {
    if (getLocalTime(&out, 10)) {
        if (out.tm_year > 120) return true; // valid (year > 2020)
    }
    return false;
}

void backend_log(const char *msg) {
    Serial.print(msg);
#if CARUSOS_USE_TELNET_LOG
    // Never touch telnetClient here (may run on Core 1); queue for Core 0.
    if (runtime_use_telnet && log_queue) {
        log_msg_t item;
        strncpy(item.msg, msg, sizeof(item.msg) - 1);
        item.msg[sizeof(item.msg) - 1] = '\0';
        xQueueSend(log_queue, &item, 0); // drop if full, never block the UI
    }
#endif
}

bool backend_get_telnet() {
    return runtime_use_telnet;
}

void backend_set_telnet(bool enable) {
    runtime_use_telnet = enable; // immediate, so getters and the accept loop see it
    backend_cmd_t cmd = { BCMD_SET_TELNET, enable ? 1 : 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

static void do_set_telnet(bool enable) {
    preferences.putBool("telnet", enable);
#if CARUSOS_USE_TELNET_LOG
    if (enable && runtime_wifi_enabled) {
        telnetServer.begin();
    } else {
        if (telnetClient) telnetClient.stop();
        telnetServer.end();
    }
#endif
}

bool backend_get_animations() {
    return runtime_use_animations;
}

void backend_set_animations(bool enable) {
    runtime_use_animations = enable;
    backend_cmd_t cmd = { BCMD_SET_ANIMATIONS, enable ? 1 : 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

static void do_set_animations(bool enable) {
    preferences.putBool("anim", enable);
}

bool backend_get_dash_alert() {
    return runtime_dash_alert;
}

void backend_set_dash_alert(bool enable) {
    runtime_dash_alert = enable;
    backend_cmd_t cmd = { BCMD_SET_DASH_ALERT, enable ? 1 : 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

static void do_set_dash_alert(bool enable) {
    preferences.putBool("dashbeep", enable);
}

void* backend_download_file(const char* url, size_t* out_size) {
    *out_size = 0;
    if (WiFi.status() != WL_CONNECTED) return NULL;
    
    HTTPClient http;
    WiFiClientSecure secureClient;
    
    if (String(url).startsWith("https")) {
        secureClient.setInsecure(); // Ignorar certificado para compatibilidad universal
        http.begin(secureClient, url);
    } else {
        http.begin(url);
    }
    
    Serial.printf("[Backend] Downloading: %s\n", url);
    int httpCode = http.GET();
    void* buffer = NULL;
    
    if (httpCode == HTTP_CODE_OK) {
        int len = http.getSize();
        if (len > 0) {
            Serial.printf("[Backend] File size: %d bytes\n", len);
            // Big image buffers belong in PSRAM; MALLOC_CAP_8BIT alone could
            // eat scarce internal heap. Fall back to internal only if needed.
            buffer = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
            if (!buffer) buffer = heap_caps_malloc(len, MALLOC_CAP_8BIT);
            if (buffer) {
                WiFiClient * stream = http.getStreamPtr();
                uint8_t* ptr = (uint8_t*)buffer;
                int remaining = len;
                unsigned long start_time = millis();
                
                while (http.connected() && remaining > 0 && (millis() - start_time < 15000)) {
                    size_t size = stream->available();
                    if (size > 0) {
                        int c_read = stream->read(ptr, size);
                        if (c_read > 0) {
                            ptr += c_read;
                            remaining -= c_read;
                            start_time = millis(); // reset timeout
                            
                            // Log every ~100KB to not flood serial
                            if (remaining % 102400 < 4096) {
                                Serial.printf("[Backend] Download progress: %d bytes remaining...\n", remaining);
                            }
                        }
                    } else {
                        delay(5);
                    }
                }
                
                if (remaining == 0) {
                    *out_size = len;
                    Serial.println("[Backend] Download complete.");
                } else {
                    Serial.printf("[Backend] ERROR: Download failed mid-way.\n");
                    Serial.printf("  -> http.connected(): %s\n", http.connected() ? "true" : "false");
                    Serial.printf("  -> Remaining bytes: %d\n", remaining);
                    Serial.printf("  -> Time elapsed since last byte: %lu ms\n", millis() - start_time);
                    free(buffer);
                    buffer = NULL;
                }
            } else {
                Serial.printf("[Backend] ERROR: Not enough PSRAM to allocate %d bytes buffer.\n", len);
            }
        } else {
            Serial.printf("[Backend] ERROR: Server did not provide Content-Length. len=%d\n", len);
        }
    } else {
        Serial.printf("[Backend] HTTP GET failed, error: %d - %s\n", httpCode, http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return buffer;
}

void backend_wifi_reconnect() {
    backend_cmd_t cmd = { BCMD_WIFI_RECONNECT, 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

static void do_wifi_reconnect() {
    Serial.println("[Backend] Manual WiFi Reconnect triggered.");
    WiFi.disconnect();
    // Reconnection is handled automatically in the while(1) loop of Task_Backend
    // because WiFi.status() will return false, triggering WiFi.begin()
}

void backend_set_wifi_enabled(bool enable) {
    runtime_wifi_enabled = enable; // immediate, so the reconnect loop honors it
    backend_cmd_t cmd = { BCMD_SET_WIFI, enable ? 1 : 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

bool backend_get_wifi_enabled() {
    return runtime_wifi_enabled;
}

static void do_set_wifi(bool enable) {
    preferences.putBool("wifi", enable);
    if (enable) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#if CARUSOS_USE_TELNET_LOG
        if (runtime_use_telnet) telnetServer.begin(); // network is back: telnet can start
#endif
    } else {
#if CARUSOS_USE_TELNET_LOG
        // Tear down telnet before the network goes away.
        if (telnetClient) telnetClient.stop();
        telnetServer.end();
#endif
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
}

bool backend_get_bluetooth() {
    return runtime_use_bluetooth;
}

void backend_set_bluetooth(bool enable) {
    runtime_use_bluetooth = enable;
    backend_cmd_t cmd = { BCMD_SET_BLUETOOTH, enable ? 1 : 0 };
    if (backend_queue) xQueueSend(backend_queue, &cmd, 0);
}

static void do_set_bluetooth(bool enable) {
    preferences.putBool("bt", enable);
#if CARUSOS_USE_BLUETOOTH
    if (enable) {
        BLEDevice::init("CarusOS");
        BLEDevice::createServer();
        BLEDevice::startAdvertising();
        Serial.println("[Backend] BLE started.");
    } else {
        BLEDevice::deinit(false);
        Serial.println("[Backend] BLE stopped.");
    }
#else
    Serial.println("[Backend] Bluetooth is disabled in compiler config.");
#endif
}

void backend_set_audio_state(bool play) {
    play_audio = play;
}

void backend_set_audio_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    audio_volume = volume;
}

int backend_get_audio_volume() {
    return audio_volume;
}

void backend_beep(int volume, int duration_ms) {
#if CARUSOS_DASHBOARD_ALERT
    if (speaker_released || play_audio) return;
    beep_volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    beep_ms = duration_ms;
#endif
}

void backend_get_sys_info(uint32_t &free_heap, uint32_t &total_heap, 
                          uint32_t &free_psram, uint32_t &total_psram,
                          uint32_t &sketch_size, uint32_t &sketch_free,
                          uint32_t &flash_size) {
    free_heap = ESP.getFreeHeap();
    total_heap = ESP.getHeapSize();
    free_psram = ESP.getFreePsram();
    total_psram = ESP.getPsramSize();
    sketch_size = ESP.getSketchSize();
    sketch_free = ESP.getFreeSketchSpace();
    flash_size = ESP.getFlashChipSize();
}
