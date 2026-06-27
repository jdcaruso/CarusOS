# CLAUDE.md

Guidance for AI assistants (Claude, Gemini, etc.) working in this repository.

## What CarusOS is

A lightweight, multi-threaded mini-OS for the **ESP32-S3** with an RGB touch
display, built on **LVGL 9.x**. Target hardware: **Waveshare ESP32-S3-Touch-LCD-4.3B**
(480x480, ST7701 panel + GT911 touch, ES8311 audio codec, XCA9554 I/O expander).

This is a hobby / learning project and a **public** repository. Keep that in mind:
no secrets in committed files, keep docs honest, prefer clarity over cleverness.

## Architecture

CarusOS splits work across the two cores of the ESP32-S3:

- **Core 1 — UI** (`ui_core.cpp`): 100% dedicated to LVGL rendering. The
  `Task_GUI` loop in `CarusOS_ESP32S3.ino` calls `lv_task_handler()` and flushes.
- **Core 0 — Backend** (`backend_core.cpp`): WiFi, NTP, audio (I2S/ES8311),
  filesystem, OTA, and the Telnet log server, all non-blocking.

The two cores talk through accessor functions in `backend_core.h` (e.g.
`backend_is_wifi_connected()`, `backend_get_time()`, `backend_set_audio_volume()`).
State shared across cores currently uses `volatile` flags.

### File map

| File | Responsibility |
|------|----------------|
| `CarusOS_ESP32S3.ino` | Entry point; creates the Core 1 GUI task |
| `carusos_config.h` | Master feature flags (Audio, FS, OTA, BT, language…) |
| `secrets.h` | WiFi/OTA credentials — **gitignored**, create from `secrets.h.example` |
| `pin_config.h` | Audio codec pins + power-chip define |
| `ui_core.cpp` | Core 1: LVGL UI, boot screen, app registry & windows |
| `backend_core.cpp` | Core 0: WiFi, NTP, audio, FS, OTA, Telnet |
| `lvgl_port.cpp` | Display/touch/LVGL hardware glue (panel, GT911, backlight) |
| `language.h` | Compile-time i18n macros (`TXT_*`), EN/ES |
| `es8311.c/.h` | Audio codec driver |
| `lv_conf.h` | LVGL configuration |

## How to add an app

Apps are defined in a registry in `ui_core.cpp`. To add one:

1. Write a builder: `static void app_build_myapp(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label)`.
   It fills the window content; the title bar / back button are already created.
2. Add one row to the `g_apps[]` array (id, icon, name, enabled, builder).
3. Reference the app's id in a menu list (desktop or Settings submenu) via
   `add_app_icon(parent, id)`.

Disabled apps (`enabled = false`) are, by default, hidden from the menus. Set
`CARUSOS_SHOW_DISABLED_APPS` to `1` in `carusos_config.h` to instead show them
greyed-out and non-clickable. Apps with a `NULL` builder are "actions" handled
in `app_launcher_event_cb` (e.g. Sleep).

## Versioning

`CARUSOS_VERSION` in `carusos_config.h` is the single source of truth, format
`vMAJOR.MINOR.PATCH`:

- **Bug fix** → bump the **PATCH** (last) digit.
- **New feature** → bump the **MINOR** (middle) digit.

When bumping, also update the README version badge and set `CARUSOS_SOURCE_DATE`
to the current date.

## Build & flash

Arduino IDE (sketch folder `ESP32-S3/CarusOS_ESP32S3/`). Before the first build,
copy `secrets.h.example` to `secrets.h` and fill in your credentials.

Critical Arduino IDE settings (see README for the full list): Board
`ESP32S3 Dev Module`, Flash `16MB`, Partition `16M Flash (3MB APP / 9.9MB FATFS)`,
PSRAM `OPI PSRAM`, Flash Mode `QIO 80MHz`. Wrong settings cause bootloops or a
very slow UI.

Dependencies: LVGL 9.x, Arduino_GFX, SensorLib (GT911), ESP_I2S.

## Conventions

- Feature flags live in `carusos_config.h`; gate optional code with `#if CARUSOS_USE_*`.
- User-facing strings go through `TXT_*` macros in `language.h` (keep EN + ES in sync).
- Never commit credentials. `secrets.h` is gitignored; only `secrets.h.example`
  is tracked.
- The repo is public — no real SSIDs, passwords, private URLs, or personal data
  in tracked files.

## Known limitations (do not "fix" silently — they are intentional/known)

- Telnet log server (port 23) is unauthenticated.
- HTTPS downloads use `setInsecure()` (no TLS cert validation).
- Cross-core state uses `volatile` flags (fine for a few flags; a FreeRTOS
  queue is the planned upgrade — see ROADMAP).
