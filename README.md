# CarusOS 📱

![Version](https://img.shields.io/badge/version-v0.37.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)
![License](https://img.shields.io/badge/license-MIT-yellow)

**CarusOS** is a lightweight, ultra-fast, multi-threaded Operating System for ESP32-S3 microcontrollers with RGB Touch displays, built heavily on top of **LVGL 9.x**. It is designed to be highly modular, extensible, and completely non-blocking.

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

To compile CarusOS correctly and avoid bootloops, you **MUST** configure your Arduino IDE exactly as follows for a standard 16MB ESP32-S3 board (like the Waveshare 4.3B):

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

> Target board: **Waveshare ESP32-S3-Touch-LCD-4.3B** (480x480, ST7701 + GT911). The pin map lives in `pin_config.h` and `lvgl_port.cpp`.

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
