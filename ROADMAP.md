# CarusOS Roadmap

## ✅ Completed Milestones

- **Phase 1: Baseline Architecture**
  - LVGL 9.x Integration with ESP32-S3.
  - OPI PSRAM Framebuffers and DMA rendering.
  - Multi-threaded core separation (UI vs Backend).

- **Phase 2: Linux Boot & Window Manager**
  - Custom boot animation with status reporting.
  - Smart Window Manager (`AUTO_DELETE`) for deep navigation.
  - Top status bar with WiFi and Clock.

- **Phase 3-4: Connectivity & UI Metaphor**
  - WiFi background connection logic.
  - Status bar real-time updates.
  - Grid-based App Launcher (Folder metaphor).

- **Phase 5-6: i18n & NTP**
  - Compile-time macro language engine (English/Spanish).
  - Background NTP Sync to GMT-3.
  - Sleep mode (XCA9554 expander backlight toggle).

- **Phase 7-9: Hardware Audio & SysInfo**
  - I2S Audio Driver (ES8311 Codec).
  - Non-blocking sine wave audio playback testing.
  - RAM, PSRAM, and Flash Storage dynamic monitoring.

- **Phase 10: OTA & File System**
  - Explicit Over-The-Air (OTA) update mode for security.
  - Internal Flash FATFS mounting and formatting.

- **Phase 11: Code Architecture** *(v0.34.0 – v0.35.0)*
  - App Registry: apps are now a `g_apps[]` table (`id`, `icon`, `name`,
    `enabled`, `build()`); adding one is "one entry + one function".
  - `CARUSOS_SHOW_DISABLED_APPS` flag (hide vs grey-out disabled apps).
  - Inter-core command queue: UI (Core 1) posts hardware/NVS actions
    (WiFi/OTA/Telnet/BLE) to a FreeRTOS queue executed on Core 0.

## 🎯 Current Focus

- **Device ↔ Server connectivity (NEXT):** Connect the device to a backend
  server — either a self-hosted machine or a cloud service. Open questions to
  decide: transport (**MQTT** vs **WebSocket** vs **REST polling**), what data
  flows (telemetry up / commands down / both), auth, and whether the server is
  local or cloud. This is the next big feature to design.

> Pending hardware test: **v0.34.0** (app registry) and **v0.35.0** (inter-core
> queue) compile clean but still need to be flashed and verified on the device.

## 🏗️ Architecture & Infrastructure

Improvements that make CarusOS a more "real" OS and an easier project to
contribute to.

- **PlatformIO support:** Add a `platformio.ini` pinning board, build flags and
  library versions, so the project builds without hand-tuning Arduino IDE.
- **CI build (GitHub Actions):** Compile on every push to catch breakage early —
  the pragmatic equivalent of tests for embedded.
- **CONTRIBUTING.md + screenshots/GIF in the README.**
- **Config hygiene (pending):** `ENABLE_APP_GIF_DEMO` and `ENABLE_APP_MIC_TEST`
  in `carusos_config.h` are defined but currently unused ("phantom" flags). Keep
  for now; either implement the apps they imply or remove them. *(Mic Test is
  planned — see below — which would give `ENABLE_APP_MIC_TEST` a real meaning.)*

## 🚀 Future Ideas (Pending)

- **Mic Test app (configurable):** A basic microphone app behind a config flag —
  mic on/off plus a live input-level meter (bar that reacts to sound). The board
  captures mic via the **ES7210 ADC** over I2S; an official driver exists in the
  Waveshare `08_ES7210` example to port. Watch out for I2S peripheral/pin sharing
  with the existing speaker (ES8311) path — likely make mic capture and audio
  playback mutually exclusive.

- **On-Screen Keyboard:** Implement an LVGL keyboard to enter WiFi credentials dynamically without hardcoding them in `secrets.h`.
- **WiFi config via UI + NVS:** Store credentials in NVS (`Preferences`) entered through the keyboard, removing the need to edit `secrets.h` for end users.
- **File Explorer:** A UI app to list files on the FATFS partition or an external MicroSD card.
- **Audio MP3 Player:** Extend the I2S driver to read `.mp3` or `.wav` files from the File System instead of generating a sine wave.
- **Deep Sleep:** True battery-saving deep sleep mode with wake-on-touch capabilities.
- **Inactivity timeout:** Auto-dim/sleep the backlight after N seconds without touch.
- **PWM brightness control:** Replace the binary backlight on/off with PWM dimming.
- **Notification / toast system:** Reusable status-bar notifications any app can raise.
- **Bluetooth:** Add BLE support for notifications from a smartphone.
