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

## 🚀 Future Ideas (Pending)

- **On-Screen Keyboard:** Implement an LVGL keyboard to enter WiFi credentials dynamically without hardcoding them in `carusos_config.h`.
- **File Explorer:** A UI app to list files on the FATFS partition or an external MicroSD card.
- **Audio MP3 Player:** Extend the I2S driver to read `.mp3` or `.wav` files from the File System instead of generating a sine wave.
- **Deep Sleep:** True battery-saving deep sleep mode with wake-on-touch capabilities.
- **Bluetooth:** Add BLE support for notifications from a smartphone.
