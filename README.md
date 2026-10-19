# CarusOS 📱

![Version](https://img.shields.io/badge/version-v0.47.1-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)
![License](https://img.shields.io/badge/license-MIT-yellow)

**CarusOS** is a lightweight, multi-threaded mini-OS for ESP32-S3 microcontrollers with RGB touch displays, built on top of **LVGL 9.x**. It is a hobby / learning project: modular, extensible, and designed so the UI never blocks on hardware or the network.

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
| Battery / PMU (AXP2101) | `CARUSOS_USE_PMU` | `pmu_axp2101.cpp` + `.h` *(full driver in XPowersLib)* | ~2.0 KB |
| WiFi · NTP · OTA · FS · Telnet · BLE | *(various)* | inline in `backend_core.cpp`, gated by `#if` | — |

## ✨ Features

- **Multi-threaded Architecture:** Core 1 is dedicated to LVGL rendering, while Core 0 handles WiFi, NTP, audio, OTA and storage. The UI hands hardware work to Core 0 through a command queue.
- **Dynamic Window Manager:** each app window is created when opened and deleted after `< Back`; everything the app allocated (timers, buffers, state) is tied to its screen and freed with it.
- **Zero-Footprint i18n:** Built-in multi-language engine using compile-time C++ macros (`CARUSOS_LANG_ES`), saving precious PSRAM.
- **Hardware Integration:** 
  - I2S Audio Driver support (ES8311 Codec).
  - Screen sleep (backlight off on GPIO4, touch wakes it) and **deep sleep** (Settings → Deep sleep; the BOOT button wakes the board).
  - External I2C Expander support (XCA9554) for the panel init, touch reset and audio amp.
- **Live Background Services:**
  - NTP Client for real-time global Clock on the Status Bar.
  - Asynchronous background WiFi handling.
- **OTA updates:** explicit, per-session Over-The-Air mode (ArduinoOTA).
- **File System (FFat) + File Explorer:** implemented, but **currently disabled** — mounting the FAT partition at boot crashes on this board. Status in [pendings.md](pendings.md).
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

Install these from the Arduino Library Manager (or as a `.zip`). The versions
listed are the ones tested on hardware and pinned in CI; newer ones may work but
aren't verified.

- **ESP32 Arduino core** 3.3.10
- **LVGL** 9.5.0 — UI engine
- **GFX Library for Arduino** (`Arduino_GFX`) **1.6.5 or newer** (CI uses 1.6.6) — RGB panel + ST7701 driver. Older releases don't compile on ESP32 core 3.3.x.
- **SensorLib** 0.4.1 — GT911 touch, QMI8658 IMU and PCF85063 RTC drivers
- **XPowersLib** 0.3.3 — AXP2101 power-management (battery) driver
- **ESP_I2S** — audio (bundled with the ESP32 Arduino core v3.x)

The ESP32 Arduino core ships the rest (`WiFi`, `Preferences`, `ArduinoOTA`, `HTTPClient`, `FFat`, `Wire`).

> Target board: **Waveshare ESP32-S3-Touch-LCD-4B** (480x480, ST7701 + GT911). The pin map lives in `pin_config.h` and `lvgl_port.cpp`.

### First-time setup (credentials & personal config)

Credentials and personal build config are **not** committed to the repo.
Before compiling:

1. Copy `customizations/secrets.h.example` to `customizations/secrets.h` and
   fill in your WiFi SSID/password and OTA password.
2. Copy `customizations/config_local.h.example` to `customizations/config_local.h`
   and fill in any personal, non-secret values (e.g. the Gallery demo image URL).

Everything in `customizations/` except the `*.example` templates is gitignored,
so your credentials and personal settings never leave your machine.

## 🛟 Backup & Recovery (boot loop / "bricked" board)

An ESP32-S3 can't really be bricked by a bad sketch: the ROM bootloader always
survives, so you can always flash it again. This section is everything needed
to get a board back: what to download, what to run, in what order.

### What you need

| Tool | What for | Where to get it |
|---|---|---|
| **esptool** (CLI) | Back up, erase and write the flash | Ships with the ESP32 Arduino core. Windows: `%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\<version>\esptool.exe` |
| **Flash Download Tool** (Windows GUI, optional) | Same as esptool, with buttons | [Espressif download](https://dl.espressif.com/public/flash_download_tool.zip) (portable, just unzip) |
| **Waveshare factory firmware** (optional) | Put the board back as it shipped | `FirmWare/*.bin` (full 16 MB image) inside [ESP32-S3-Touch-LCD-4B-Demos.zip](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B/ESP32-S3-Touch-LCD-4B-Demos.zip) |

Commands below use `COM3`; use your port (Arduino IDE → Tools → Port). They use
esptool 5 syntax; older versions spell it `read_flash` / `write_flash` /
`erase_region` / `erase_flash`.

### 1. Before experimenting: back up the working flash

Before anything risky (enabling the File System, OTA tests, big refactors), take
a full copy of the board while it works. It takes ~4 minutes and only reads:

```bash
esptool --port COM3 --baud 921600 read-flash 0 0x1000000 fulldump.bin
```

> ⚠️ The dump (and any compiled `.bin`) contains **your WiFi SSID and password**,
> because `secrets.h` is compiled in. Keep it on your machine, never in the repo
> (`*.bin` is gitignored) or anywhere public.

### 2. If the board boot-loops, go step by step

1. **Flash a working build.** Set the offending flag back to 0 and upload as
   usual from the Arduino IDE. If the upload can't connect because the board
   keeps rebooting, put it in **download mode**: hold **BOOT**, tap **RESET**,
   release **BOOT**, then upload again. Press **RESET** afterwards to boot normally.
2. **Still looping? Wipe only the FAT partition.** Leftover data from earlier
   flashes in the file-system region can crash the FFat mount at boot. This keeps
   the app and saved settings:

   ```bash
   esptool --port COM3 erase-region 0x610000 0x9E0000
   ```

   (Offsets of the `ffat` partition in the `16M Flash (3MB APP / 9.9MB FATFS)` scheme.)
3. **Restore your backup** (exact state from step 1, settings included):

   ```bash
   esptool --port COM3 --baud 921600 write-flash 0x0 fulldump.bin
   ```

4. **No backup? Factory reset or erase everything.** Write the Waveshare factory
   image at `0x0` the same way, or wipe it all with
   `esptool --port COM3 erase-flash`, then upload CarusOS again. Saved settings
   (WiFi on/off, Telnet, animations) go back to their defaults.

### Same thing with the GUI (Flash Download Tool)

- Start it and choose **ChipType `ESP32-S3`**, **WorkMode `Develop`**,
  **LoadMode `UART`**.
- **Restore / factory reset:** first row → your `fulldump.bin` (or the factory
  `.bin`) at address **`0x0`**, ticked; pick the COM port and BAUD 921600, then
  **START**. Wait for **FINISH**, then press **RESET** on the board.
- **Erase:** the **ERASE** button wipes the whole flash (same as `erase-flash`).
- It can't make the backup in step 1 for you; use esptool for that.

## 📁 Project Structure

All of it lives in the sketch folder `ESP32-S3/CarusOS_ESP32S3/`:

- `carusos_config.h`: Master configuration file. Turn features (Audio, FS, OTA, apps) ON/OFF here to save RAM/Flash.
- `pin_config.h`: every pin (audio codec, I2C bus, I/O expander).
- `ui_core.cpp`: Core 1 shell — boot screen, desktop, app registry, windows.
- `src/apps/`: the apps, one file each (or one per group of small ones).
- `backend_core.cpp`: Core 0 background logic (WiFi, NTP, RTC, battery/PMU, audio, FS, OTA, Telnet).
- `lvgl_port.cpp`: display, touch and backlight glue for the Waveshare board.
- `customizations/secrets.h`: Your WiFi/OTA credentials (gitignored — create it from `customizations/secrets.h.example`).
- `customizations/config_local.h`: Personal, non-secret build config like the Gallery demo URL (gitignored — create it from `customizations/config_local.h.example`).

## 🔒 Security Notes

CarusOS is a hobby/learning project. A few things are intentionally relaxed and should be hardened before any non-trivial deployment:

- **Telnet log server (port 23)** is unauthenticated — anyone on the same network can read the logs while it's enabled.
- **HTTPS downloads use `setInsecure()`** — TLS certificates are not validated.
- **OTA** is protected only by the password you set in `customizations/secrets.h`. Use a strong one.

## 🔐 Personal Customizations

CarusOS is public, so nothing personal — credentials or otherwise — can live in
tracked files. Anything like that goes in
[`customizations/`](ESP32-S3/CarusOS_ESP32S3/customizations/), split in two files:

- **`secrets.h`** — actual credentials (WiFi SSID/password, OTA password).
- **`config_local.h`** — personal, non-secret build settings that aren't generic
  enough to hardcode in the public repo (e.g. a URL you point a demo app at).

Both are gitignored; only their `*.example` counterparts (with placeholder
values) are tracked, so anyone cloning the repo gets a working template and
fills in their own values. This is enforced by CI: [`build.yml`](.github/workflows/build.yml)
generates dummy `secrets.h`/`config_local.h` from the `*.example` files and
does a real compile on every push — if a real file and its `.example` fall out
of sync (a new `#define` added to one but not the other), the build fails
before it reaches `main`.

If you add your own personal override file, follow the same pattern: create
`customizations/yourfile.h` (real values, never committed) plus
`customizations/yourfile.h.example` (placeholder values, tracked), `#include`
it from [`carusos_config.h`](ESP32-S3/CarusOS_ESP32S3/carusos_config.h), and
add the CI step to generate the dummy version.

## 📄 License

Released under the [MIT License](LICENSE).

---

Built by Javier Caruso → [javiercaruso.com](https://javiercaruso.com)
