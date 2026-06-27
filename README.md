# CarusOS 📱

![Version](https://img.shields.io/badge/version-v0.39.1-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)
![License](https://img.shields.io/badge/license-MIT-yellow)

**CarusOS** is a lightweight, ultra-fast, multi-threaded Operating System for ESP32-S3 microcontrollers with RGB Touch displays, built heavily on top of **LVGL 9.x**. It is designed to be highly modular, extensible, and completely non-blocking.

## 🧩 Configuration-Based — pay only for what you use

CarusOS is **modular by compile-time configuration**. Every feature — Audio, WiFi,
NTP, OTA, the File System, even individual apps — is gated behind a flag in
[`carusos_config.h`](ESP32-S3/CarusOS_ESP32S3/carusos_config.h). Flip a flag off and
that feature **compiles out completely**: its code, its libraries, and its RAM all
disappear from the binary. A disabled feature is **not** dead weight you carry "just
in case" — it simply isn't there.

This is arguably CarusOS's best trait. The app partition is only **3 MB**, so every
kilobyte counts: a trimmed-down build leaves room for *your* apps. Don't need sound?
`CARUSOS_USE_AUDIO 0`. Don't need a clock, an explorer, or Bluetooth? Off they go, and
the OS shrinks to fit. Build the device *you* want, not the one someone else imagined.

### 📊 Feature source sizes (in-sketch drivers)

These are the **real sizes of the driver source files bundled in the sketch** for each
hardware feature — a "how much code does this pull in" reference, *not* a compiled
flash-footprint measurement.

> ⚠️ This only counts the sketch's own files. Features that live in the ESP32 Arduino
> core or external libraries (WiFi, OTA, FFat, and especially **BLE ≈ 1 MB**) have
> little or no dedicated file here yet weigh far more in the final binary. The
> always-on baseline (LVGL + Arduino_GFX + WiFi/core) dominates the binary regardless.
> Real per-feature flash numbers need differential builds — see [pendings.md](pendings.md).

| Feature | Flag | In-sketch source files | Size |
|---|---|---|---|
| Audio (ES8311) | `CARUSOS_USE_AUDIO` | `es8311.c` + `es8311.h` + `es8311_reg.h` + `audio_hal.h` | ~33 KB |
| Mic Test (ES7210) | `ENABLE_APP_MIC_TEST` | `es7210.cpp` + `es7210.h` + `mic_es7210.cpp` + `mic_es7210.h` | ~31 KB |
| IMU / Motion | `ENABLE_APP_IMU` | `imu_qmi8658.cpp` + `.h` *(full driver in SensorLib)* | ~1.8 KB |
| RTC clock | `CARUSOS_USE_RTC` | `rtc_pcf85063.cpp` + `.h` *(full driver in SensorLib)* | ~1.5 KB |
| WiFi · NTP · OTA · FS · Telnet · BLE | *(various)* | inline in `backend_core.cpp`, gated by `#if` | — |

## ✨ Features

- **Multi-threaded Architecture:** Core 1 is 100% dedicated to UI rendering (60 FPS on LVGL 9), while Core 0 handles WiFi, NTP, and Audio processing asynchronously.
- **Dynamic Window Manager:** Linux-style memory management. Windows (Apps) are instantiated dynamically when opened and their RAM is freed automatically when the `< Back` button is pressed using `LV_OBJ_FLAG_AUTO_DELETE`.
- **Zero-Footprint i18n:** Built-in multi-language engine using compile-time C++ macros (`CARUSOS_LANG_ES`), saving precious PSRAM.
- **Hardware Integration:** 
  - I2S Audio Driver support (ES8311 Codec).
  - External I2C Expander support (e.g. XCA9554) for instant Backlight toggling / Sleep Mode.
- **Live Background Services:**
  - NTP Client for real-time global Clock on the Status Bar.
  - Asynchronous background WiFi handling.
- **Extensible File System & OTA:** Built-in support for FFat and explicit Over-The-Air updates.
- **Remote Telnet Logging:** Debug your OS wirelessly without a serial cable! Just enable `CARUSOS_USE_TELNET_LOG` in `carusos_config.h`, connect your PC to the same WiFi, and run `telnet <ESP32_IP>` in your terminal. You can see the IP in the SysInfo app.

## 🚀 Compilation Settings (CRITICAL)

To compile CarusOS correctly and avoid bootloops, you **MUST** configure your Arduino IDE exactly as follows for a standard 16MB ESP32-S3 board (like the Waveshare 4B):

1. **Board:** `ESP32S3 Dev Module`
2. **Flash Size:** `16MB (128Mb)`
3. **Partition Scheme:** `16M Flash (3MB APP / 9.9MB FATFS)`
4. **PSRAM:** `OPI PSRAM`
5. **Flash Mode:** `QIO 80MHz` *(If you select DIO, the OS will run extremely slow!)*
6. **USB CDC On Boot:** `Enabled`

### Dependencies

Install these from the Arduino Library Manager (or as a `.zip`):

- **LVGL** (v9.x) — UI engine
- **GFX Library for Arduino** (`Arduino_GFX`) — RGB panel + ST7701 driver
- **SensorLib** — provides the `TouchDrvGT911` capacitive touch driver
- **ESP_I2S** — audio (bundled with the ESP32 Arduino core v3.x)

The ESP32 Arduino core (v3.x) ships the rest (`WiFi`, `Preferences`, `ArduinoOTA`, `HTTPClient`, `FFat`, `Wire`).

> Target board: **Waveshare ESP32-S3-Touch-LCD-4B** (480x480, ST7701 + GT911). The pin map lives in `pin_config.h` and `lvgl_port.cpp`.

### First-time setup (credentials)

Credentials are **not** committed to the repo. Before compiling:

1. Copy `secrets.h.example` to `secrets.h` (same folder).
2. Open `secrets.h` and fill in your WiFi SSID/password and OTA password.

`secrets.h` is gitignored, so your credentials never leave your machine.

## 📁 Project Structure

CarusOS is structured to be easily portable.
- `carusos_config.h`: Master configuration file. Turn features (Audio, FS, OTA) ON/OFF here to save RAM/Flash.
- `ui_core.cpp`: Core 1 UI logic (LVGL).
- `backend_core.cpp`: Core 0 Background logic (WiFi, NTP, FS).
- `secrets.h`: Your WiFi/OTA credentials (gitignored — create it from `secrets.h.example`).

## 🔒 Security Notes

CarusOS is a hobby/learning project. A few things are intentionally relaxed and should be hardened before any non-trivial deployment:

- **Telnet log server (port 23)** is unauthenticated — anyone on the same network can read the logs while it's enabled.
- **HTTPS downloads use `setInsecure()`** — TLS certificates are not validated.
- **OTA** is protected only by the password you set in `secrets.h`. Use a strong one.

## 📄 License

Released under the [MIT License](LICENSE).
