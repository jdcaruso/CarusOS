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

## 🏗️ Architecture & Infrastructure

Improvements that make CarusOS a more "real" OS and an easier project to
contribute to.

- **App Registry (in progress):** Replace the hard-coded `switch(app_id)` with a
  table of apps (`id`, `icon`, `name`, `enabled`, `build()`). Adding an app
  becomes "one entry + one function" instead of editing the core in several
  places. *(See `ui_core.cpp` and the "How to add an app" section in `CLAUDE.md`.)*
- **Inter-core message queue:** Replace the `volatile` flags shared between the
  UI (Core 1) and backend (Core 0) with a FreeRTOS queue. Scales better than
  ad-hoc flags and avoids cross-thread access to WiFi/driver state.
- **PlatformIO support:** Add a `platformio.ini` pinning board, build flags and
  library versions, so the project builds without hand-tuning Arduino IDE.
- **CI build (GitHub Actions):** Compile on every push to catch breakage early —
  the pragmatic equivalent of tests for embedded.
- **CONTRIBUTING.md + screenshots/GIF in the README.**

## 🚀 Future Ideas (Pending)

- **On-Screen Keyboard:** Implement an LVGL keyboard to enter WiFi credentials dynamically without hardcoding them in `secrets.h`.
- **WiFi config via UI + NVS:** Store credentials in NVS (`Preferences`) entered through the keyboard, removing the need to edit `secrets.h` for end users.
- **File Explorer:** A UI app to list files on the FATFS partition or an external MicroSD card.
- **Audio MP3 Player:** Extend the I2S driver to read `.mp3` or `.wav` files from the File System instead of generating a sine wave.
- **Deep Sleep:** True battery-saving deep sleep mode with wake-on-touch capabilities.
- **Inactivity timeout:** Auto-dim/sleep the backlight after N seconds without touch.
- **PWM brightness control:** Replace the binary backlight on/off with PWM dimming.
- **Notification / toast system:** Reusable status-bar notifications any app can raise.
- **Bluetooth:** Add BLE support for notifications from a smartphone.
