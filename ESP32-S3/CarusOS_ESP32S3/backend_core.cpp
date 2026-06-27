#include "backend_core.h"
#include "carusos_config.h"
#include "lvgl_port.h"
#include <WiFi.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#if CARUSOS_USE_OTA
#include <ArduinoOTA.h>
#endif

#include <Preferences.h>
Preferences preferences;
bool runtime_use_telnet = true;
bool runtime_use_animations = false;
bool runtime_use_bluetooth = false;

#if CARUSOS_USE_BLUETOOTH
#include <BLEDevice.h>
#include <BLEServer.h>
#endif

#include <WiFiServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
WiFiServer telnetServer(23);
WiFiClient telnetClient;

#if CARUSOS_USE_FS
#include <FFat.h>
#include <esp_partition.h>
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
static volatile bool ota_enabled = false;

void backend_enable_ota() {
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

String backend_get_ip() {
    if (wifi_connected) {
        return WiFi.localIP().toString();
    }
    return "Disconnected";
}

String backend_list_files() {
    String output = "";
#if CARUSOS_USE_FS
    const esp_partition_t* fat_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
    if (fat_part == NULL) {
        return "ERROR: FAT Partition not found.\nCheck Arduino IDE settings.";
    }

    File root = FFat.open("/");
    if (!root) {
        return "ERROR: Failed to open root directory.";
    }
    if (!root.isDirectory()) {
        return "ERROR: Root is not a directory.";
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            output += String(file.name()) + " - " + String(file.size() / 1024) + " KB\n";
        }
        file = root.openNextFile();
    }
    
    if (output == "") {
        output = "Disk is empty.";
    }
#else
    output = "File System Disabled in Config.";
#endif
    return output;
}

#if CARUSOS_USE_AUDIO
static void Task_Audio(void *pvParameters) {
// ... same as before

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

static void Task_Backend(void *pvParameters) {
    // Wait a bit for the system to settle
    vTaskDelay(pdMS_TO_TICKS(2000));

#if CARUSOS_USE_AUDIO
    // Start Audio Task on same Core 0
    xTaskCreatePinnedToCore(Task_Audio, "TaskAudio", 8192, NULL, 1, &TaskAudio, 0);
#endif

    Serial.println("[Backend] Starting WiFi Connection...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

#if CARUSOS_USE_FS
    Serial.println("[Backend] Checking for FAT partition...");
    const esp_partition_t* fat_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
    
    if (fat_part == NULL) {
        Serial.println("[Backend] CRITICAL: FAT Partition not found in flash!");
        Serial.println("[Backend] The OS will continue, but File System is disabled.");
        Serial.println("[Backend] To fix this, change Partition Scheme in Arduino IDE.");
    } else {
        Serial.println("[Backend] Mounting FFat...");
        if (FFat.begin(true)) { // true = format if mount fails
            Serial.println("[Backend] FFat Mounted.");
        } else {
            Serial.println("[Backend] FFat Mount Failed.");
        }
    }
#endif

    preferences.begin("carusos", false);
    runtime_use_telnet = preferences.getBool("telnet", true);
    runtime_use_animations = preferences.getBool("anim", false);
    runtime_use_bluetooth = preferences.getBool("bt", false);

#if CARUSOS_USE_BLUETOOTH
    if (runtime_use_bluetooth) {
        BLEDevice::init("CarusOS");
        BLEDevice::createServer();
        BLEDevice::startAdvertising();
        Serial.println("[Backend] BLE Advertising started.");
    }
#endif

    if (runtime_use_telnet) {
        telnetServer.begin();
    }

    while (1) {
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
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Reconnect
            }
        }

        if (ota_enabled) {
#if CARUSOS_USE_OTA
            ArduinoOTA.handle();
#endif
            vTaskDelay(pdMS_TO_TICKS(10)); // Poll quickly for OTA
        } else {
            vTaskDelay(pdMS_TO_TICKS(50)); // Poll every 50ms normally
        }

        if (runtime_use_telnet) {
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
    }
}

void backend_core_init() {
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
}

bool backend_is_wifi_connected() {
    return wifi_connected;
}

bool backend_get_time(int &hour, int &minute) {
    if (!wifi_connected) return false;
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) { // 10ms timeout
        if (timeinfo.tm_year > 120) { // Make sure year is valid (>2020)
            hour = timeinfo.tm_hour;
            minute = timeinfo.tm_min;
            return true;
        }
    }
    return false;
}

void backend_log(const char *msg) {
    Serial.print(msg);
    if (runtime_use_telnet && telnetClient && telnetClient.connected()) {
        telnetClient.print(msg);
    }
}

bool backend_get_telnet() {
    return runtime_use_telnet;
}

void backend_set_telnet(bool enable) {
    runtime_use_telnet = enable;
    preferences.putBool("telnet", enable);
    if (enable) {
        telnetServer.begin();
    } else {
        if (telnetClient) telnetClient.stop();
        telnetServer.end();
    }
}

bool backend_get_animations() {
    return runtime_use_animations;
}

void backend_set_animations(bool enable) {
    runtime_use_animations = enable;
    preferences.putBool("anim", enable);
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
            buffer = heap_caps_malloc(len, MALLOC_CAP_8BIT); // PSRAM
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
    Serial.println("[Backend] Manual WiFi Reconnect triggered.");
    WiFi.disconnect();
    // Reconnection is handled automatically in the while(1) loop of Task_Backend
    // because WiFi.status() will return false, triggering WiFi.begin()
}

bool backend_get_bluetooth() {
    return runtime_use_bluetooth;
}

void backend_set_bluetooth(bool enable) {
    runtime_use_bluetooth = enable;
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
