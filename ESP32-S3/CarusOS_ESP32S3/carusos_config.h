#pragma once

// CarusOS Global Configuration

// WiFi / OTA credentials live in secrets.h (gitignored).
// Copy secrets.h.example to secrets.h and fill in your values.
#include "secrets.h"

#define CARUSOS_VERSION         "v0.39.0"
#define CARUSOS_AUTHOR          "Javier D. Caruso"
#define CARUSOS_SOURCE_DATE     "2026-06-27"
#define CARUSOS_LANG_ES         1 // 1 for Spanish, 0 for English

// NTP Settings
#define NTP_SERVER      "pool.ntp.org"
#define GMT_OFFSET_SEC  -10800 // GMT-3
#define DAYLIGHT_OFFSET_SEC 0

// I2S Audio Pins
#define PIN_ES7210_BCLK       16
#define PIN_ES7210_LRCK       7
#define PIN_ES7210_DIN        15
#define PIN_ES7210_MCLK       5
#define PIN_ES8311_DOUT       6

// Configuraciones del Sistema (Core)
#define CARUSOS_USE_WIFI        1
#define CARUSOS_USE_NTP_TIME    1
#define CARUSOS_USE_RTC         1 // 1: PCF85063 RTC (offline clock). 0: no se compila.
#define CARUSOS_USE_BLUETOOTH   0 // 0: Compile sin Bluetooth, 1: Compilar soporte BT
#define CARUSOS_USE_AUDIO       1 // 0: Disable Audio and save RAM/Flash, 1: Enable ES8311 Audio   
#define CARUSOS_USE_FS          0 // 1: Enable FFat File System (needs FATFS partition scheme)
#define CARUSOS_USE_TELNET_LOG  1 // 1: Enable Telnet Log Server on port 23
#define CARUSOS_USE_OTA         1 // 1: Enable OTA Updates
#define CARUSOS_DARK_MODE       1   

// Configuraciones OTA (Over-The-Air)
// CARUSOS_OTA_PASSWORD se define en secrets.h
#define CARUSOS_OTA_HOSTNAME    "CarusOS_ESP32"
#define CARUSOS_USE_HTTP_OTA    1
#define OTA_CHECK_INTERVAL_MIN  60
#define OTA_UPDATE_URL          "http://example.com/carusos/firmware.bin" // Cambiar por tu servidor

// Aplicaciones Habilitadas (GUI)
// 0: Ocultar apps deshabilitadas del menu (recomendado).
// 1: Mostrarlas grisadas/no clickeables.
#define CARUSOS_SHOW_DISABLED_APPS 0
#define ENABLE_APP_SETTINGS     1
#define CARUSOS_APP_EXPLORER    0 // 1: Enable File Explorer App (WIP: causaba crash, en debug)
#define CARUSOS_APP_GALLERY     0 // 0: Disable Web Gallery, 1: Enable Web Gallery
#define CARUSOS_USE_PNG         0 // 0: Disable PNG decoding (saves RAM/Flash), 1: Enable PNG decoding
#define ENABLE_APP_GIF_DEMO     1
#define ENABLE_APP_MIC_TEST     1
#define ENABLE_APP_ANIM         0 // 0: App de animacion no se compila ni aparece. 1: Habilitada.
#define ENABLE_APP_IMU          1 // 1: App de sensor de movimiento (IMU QMI8658). 0: no se compila.
