#pragma once

// CarusOS Global Configuration

// Personal overrides live in customizations/ (gitignored). Copy
// customizations/config_local.h.example to config_local.h and
// customizations/secrets.h.example to secrets.h, then fill in your values.
// secrets.h (WiFi/OTA credentials) is included only by backend_core.cpp, the
// one file that needs it, so the rest of the build never sees the passwords.
#include "customizations/config_local.h"

#define CARUSOS_VERSION         "v0.49.0"
#define CARUSOS_AUTHOR          "Javier D. Caruso"
#define CARUSOS_SOURCE_DATE     "2026-10-21"
#define CARUSOS_LANG_ES         1 // 1 for Spanish, 0 for English

// NTP Settings
#define NTP_SERVER      "pool.ntp.org"
#define GMT_OFFSET_SEC  -10800 // GMT-3
#define DAYLIGHT_OFFSET_SEC 0

// All pins (audio, I2C, I/O expander) live in pin_config.h.

// System features
#define CARUSOS_USE_WIFI        1 // Must stay 1: compiling WiFi out is not implemented yet.
#define CARUSOS_USE_NTP_TIME    1
#define CARUSOS_USE_RTC         1 // 1: PCF85063 RTC (offline clock). 0: compiled out.
#define CARUSOS_USE_BLUETOOTH   0 // 0: no Bluetooth, 1: compile BLE support (~1 MB of flash)
#define CARUSOS_USE_AUDIO       1 // 0: Disable Audio and save RAM/Flash, 1: Enable ES8311 Audio
#define CARUSOS_USE_FS          0 // 1: Enable FFat File System (needs FATFS partition scheme).
                                  // Currently 0: mounting the FAT partition at boot crashes on
                                  // this board. Status and next steps: pendings.md -> Known bugs.
#define CARUSOS_USE_TELNET_LOG  1 // 1: Telnet log server on port 23 (on/off at runtime in Options). 0: compiled out.
#define CARUSOS_USE_OTA         1 // 1: Enable OTA Updates
#define CARUSOS_USE_PMU         1 // 1: AXP2101 battery/USB status (status bar + Sys Info). 0: compiled out.
#define CARUSOS_USE_DEEP_SLEEP  1 // 1: "Deep sleep" action in Settings; the BOOT button wakes it. 0: compiled out.

// OTA (Over-The-Air). CARUSOS_OTA_PASSWORD is defined in secrets.h.
// (Only ArduinoOTA is implemented; the HTTP-pull OTA config was never wired up
//  and has been removed to avoid phantom settings.)
#define CARUSOS_OTA_HOSTNAME    "CarusOS_ESP32"

// Apps (GUI)
// 0: hide disabled apps from the menus (recommended).
// 1: show them greyed out and non-clickable.
#define CARUSOS_SHOW_DISABLED_APPS 0
#define CARUSOS_APP_EXPLORER    0 // 1: File Explorer (FS ops run on Core 0). Needs CARUSOS_USE_FS.
#define CARUSOS_APP_GALLERY     1 // 0: Disable Web Gallery, 1: Enable Web Gallery. Needs CARUSOS_USE_PNG.
#define CARUSOS_USE_PNG         1 // 0: Disable PNG decoding (saves RAM/Flash), 1: Enable PNG decoding
#define ENABLE_APP_MIC_TEST     1 // 1: ES7210 mic level meter. 0: compiled out.
#define ENABLE_APP_ANIM         1 // 1: bouncing-sprite demo. 0: compiled out.
#define ENABLE_APP_IMU          1 // 1: tilt demo on the QMI8658 IMU. 0: compiled out.
#define CARUSOS_APP_DASHBOARD   1 // 1: Dashboard cards (countdowns set in config_local.h). 0: compiled out.
#define CARUSOS_DASHBOARD_ALERT 1 // 1: soft beep before each countdown (on/off in Options). Needs AUDIO.
// Defaults for the beep; override them in customizations/config_local.h.
#ifndef CARUSOS_DASHBOARD_ALERT_LEAD
#define CARUSOS_DASHBOARD_ALERT_LEAD   600 // seconds before an event (per-event 4th field overrides; 0 = none)
#endif
#ifndef CARUSOS_DASHBOARD_ALERT_VOLUME
#define CARUSOS_DASHBOARD_ALERT_VOLUME 30  // 0-100, codec volume for the beep only
#endif
#define CARUSOS_DASHBOARD_REMOTE 1 // 1: Dashboard also shows items from a JSON URL polled over WiFi. 0: compiled out.
// URL and poll interval; set the URL in customizations/config_local.h (empty = off).
#ifndef CARUSOS_DASHBOARD_REMOTE_URL
#define CARUSOS_DASHBOARD_REMOTE_URL    ""
#endif
#ifndef CARUSOS_DASHBOARD_REMOTE_POLL_S
#define CARUSOS_DASHBOARD_REMOTE_POLL_S 90 // seconds between polls
#endif

// ---------------------------------------------------------------------------
// Sanity checks: fail the build on flag combinations that cannot work, instead
// of shipping an app that only says "disabled".
// ---------------------------------------------------------------------------
#if !CARUSOS_USE_WIFI
#error "CARUSOS_USE_WIFI=0 is not implemented yet (WiFi is always compiled in)."
#endif
#if CARUSOS_APP_EXPLORER && !CARUSOS_USE_FS
#error "CARUSOS_APP_EXPLORER needs CARUSOS_USE_FS=1."
#endif
#if CARUSOS_DASHBOARD_ALERT && !(CARUSOS_APP_DASHBOARD && CARUSOS_USE_AUDIO)
#error "CARUSOS_DASHBOARD_ALERT needs CARUSOS_APP_DASHBOARD=1 and CARUSOS_USE_AUDIO=1."
#endif
#if CARUSOS_DASHBOARD_REMOTE && !CARUSOS_APP_DASHBOARD
#error "CARUSOS_DASHBOARD_REMOTE needs CARUSOS_APP_DASHBOARD=1."
#endif
#if CARUSOS_APP_GALLERY && !CARUSOS_USE_PNG
#error "CARUSOS_APP_GALLERY needs CARUSOS_USE_PNG=1 (it shows a downloaded PNG)."
#endif
