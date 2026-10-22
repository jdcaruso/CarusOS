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

- **Phase 11: Code Architecture & first apps** *(v0.34.0 – v0.36.0)*
  - App Registry: apps are now a `g_apps[]` table (`id`, `icon`, `name`,
    `enabled`, `build()`); adding one is "one entry + one function".
  - `CARUSOS_SHOW_DISABLED_APPS` flag (hide vs grey-out disabled apps).
  - Inter-core command queue: UI (Core 1) posts hardware/NVS actions
    (WiFi/OTA/Telnet/BLE) to a FreeRTOS queue executed on Core 0.
  - Animation app (`ENABLE_APP_ANIM`): code-driven LVGL sprite demo.
  - Mic Test app (`ENABLE_APP_MIC_TEST`): ES7210 capture + live level meter.

- **Phase 12: Onboard hardware & hardening** *(v0.37.0 – v0.47.0)*
  - Motion app (QMI8658 IMU), Date & Time app with the PCF85063 RTC (clock
    survives reboots without WiFi).
  - WiFi on/off persisted in NVS; tabbed Sys Info with the chip list.
  - v0.45.0: libraries updated (LVGL 9.5.0, SensorLib 0.4.1, Arduino_GFX
    1.6.6); Animation and Mic Test apps enabled.
  - v0.46.0: AXP2101 battery/USB status in the status bar and Sys Info.
  - v0.47.0: backlight moved to its real pin (GPIO4); deep sleep, woken by BOOT.
  - v0.48.0: Dashboard app, first card type: recurring countdowns configured
    in `config_local.h`, with an optional soft beep before each one.
  - Telnet log goes through a queue (Core 0 owns the socket); FS work moved to
    Core 0; expander mutex; framebuffer pushed only when dirty.
  - Compile-check CI with `arduino-cli`, libraries pinned.
  - Personal config split into `customizations/` (`secrets.h`, `config_local.h`).
  - Apps split into `src/apps/`, with every app's resources tied to its screen
    (`LV_EVENT_DELETE`).

## 🎯 Current Focus

- **Device ↔ Server connectivity (NEXT):** Connect the device to a backend
  server — either a self-hosted machine or a cloud service. Open questions to
  decide: transport (**MQTT** vs **WebSocket** vs **REST polling**), what data
  flows (telemetry up / commands down / both), auth, and whether the server is
  local or cloud. This is the next big feature to design.

> Pending hardware test and open bugs (File System, Mic/speaker hand-off) are
> tracked in [pendings.md](pendings.md).

## 🏗️ Architecture & Infrastructure

Improvements that make CarusOS a more "real" OS and an easier project to
contribute to.

- **PlatformIO support:** Add a `platformio.ini` pinning board, build flags and
  library versions, so the project builds without hand-tuning Arduino IDE. It
  also unlocks a custom sdkconfig (e.g. IRAM-safe RGB/GDMA ISRs for flash writes
  while the panel runs).
- **CONTRIBUTING.md + screenshots/GIF in the README.**
- **Compile WiFi out:** `CARUSOS_USE_WIFI` must stay 1 for now (an `#error`
  enforces it). Making it a real flag means gating NTP, OTA, Telnet and the
  Gallery behind it.
- **Build-size discipline:** every optional feature must compile out completely
  when its flag is 0 (zero flash/RAM). Track the compiled size per release —
  the Arduino IDE prints `Sketch uses N bytes (X%)` after each build. Big-ticket
  items to watch: BLE (~1 MB), PNG decoder, image/GIF assets.

## 🧭 Onboard Hardware Not Yet Used

The Waveshare ESP32-S3-Touch-LCD-4B has several chips we haven't exposed yet
(confirmed by the board's `examples/` demos). Candidates for the basic feature
suite:

- **QMI8658 — 6-axis IMU (accelerometer + gyroscope).** Motion/tilt/orientation:
  auto-rotate the screen, step counter, level/bubble, shake gestures, tilt games.
- **PCF85063 — RTC (battery-backed real-time clock).** Keep the time without
  WiFi/NTP; survives reboots. Today the clock only comes from NTP.
- **AXP2101 — power management IC.** *Done in v0.46.0:* battery %, voltage, USB
  and charging in the status bar and Sys Info (`CARUSOS_USE_PMU`). Still open:
  low-battery warning, reading the PWR key (EXIO4).
- ~~**microSD slot (SD_MMC).**~~ **Not on this board.** The official 4B
  schematic has no TF socket and no free GPIOs for one (checked 2026-10-19).
  Storage stays the internal FATFS partition, or an external SPI module on a
  different board.

Board facts from the official schematic (`ESP32-S3-Touch-LCD-4B.pdf`), so nobody
has to re-derive them:

- **Backlight = GPIO4** (`BL`, into an AP3032 LED driver, PWM-capable), **active low**: low = lit (measured).
- **TCA9554 expander:** EXIO0-2 LCD SPI (CS/SDA/SCL, panel init), EXIO3 amp
  enable, EXIO4 PWR key state (low while pressed), EXIO5 touch reset, EXIO6 touch
  INT, EXIO7 LCD reset. The expander's own INT pin is not wired to the ESP32.
- **No interrupt line reaches the ESP32** from the touch, the AXP2101, the RTC
  or the IMU. Deep-sleep wake sources: the BOOT button (GPIO0) and the timer.
- PWR key goes to the AXP2101 `PWRON` input (power on/off in hardware).

## 🚀 Future Ideas (Pending)

- **OTA: multiple update URLs (failover).** Check 2–3 update URLs in order so a
  single host being down doesn't block updates. Store the list in `secrets.h` /
  config; try each until one responds. *(Note: HTTP OTA update flow is not yet
  hardware-tested.)*
- **On-Screen Keyboard:** Implement an LVGL keyboard to enter WiFi credentials dynamically without hardcoding them in `secrets.h`.
- **WiFi config via UI + NVS:** Store credentials in NVS (`Preferences`) entered through the keyboard, removing the need to edit `secrets.h` for end users.
- **File Explorer v2:** the app exists (lists the FATFS partition, formats on
  demand) but is off until the FS boot bug is fixed. Next: browse folders, open
  files.
- **Audio MP3 Player:** Extend the I2S driver to read `.mp3` or `.wav` files from the File System instead of generating a sine wave.
- **Deep Sleep:** *done in v0.47.0*, woken by the BOOT button. Wake-on-touch is
  not possible on this board (the touch INT only reaches the I/O expander, whose
  INT is not wired to the ESP32). A full power-off through the AXP2101 (PWR key
  turns it back on) is the other option, not done.
- **Inactivity timeout:** Auto-dim/sleep the backlight after N seconds without touch.
- **PWM brightness control:** *done in v0.50.0* (Options slider, 10–100 %, NVS).
- **Dashboard: more card types.** Next one: a remote JSON card that polls a
  URL served by a separate backend (the device holds no third-party tokens;
  the backend does the API work and returns ready-to-show values). The card
  code is laid out for this (`dash_card_t` with a per-type tick).
- **Notification / toast system:** Reusable status-bar notifications any app can raise.
- **Bluetooth:** Add BLE support for notifications from a smartphone.
