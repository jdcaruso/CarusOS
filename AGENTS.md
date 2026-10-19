# AGENTS.md

Guidance for AI assistants (Claude, Codex, Gemini, …) working in this repository.
This is the single canonical copy: `CLAUDE.md` and `GEMINI.md` only point here.

## What CarusOS is

A lightweight, multi-threaded mini-OS for the **ESP32-S3** with an RGB touch
display, built on **LVGL 9.x**. Target hardware: **Waveshare ESP32-S3-Touch-LCD-4B**
(480x480, ST7701 panel + GT911 touch, ES8311 audio codec, XCA9554 I/O expander,
AXP2101 power management). Pin facts from the official schematic: `ROADMAP.md`
→ onboard chips.

This is a hobby / learning project and a **public** repository. Keep that in mind:
no secrets in committed files, keep docs honest, prefer clarity over cleverness.

## Where things are written down

Each kind of information has exactly one home — update it there, don't start a
new status file:

| What | Where |
|------|-------|
| How to build, features, user-facing docs | `README.md` |
| Rules for agents, architecture, conventions | `AGENTS.md` (this file) |
| Strategic direction, feature ideas | `ROADMAP.md` |
| Concrete TODOs and **known bugs** (with current status) | `pendings.md` |

If a debugging session learns something (a root cause, a workaround, a
hypothesis ruled out), write it into the relevant `pendings.md` entry before
the session ends. Don't leave a pointer to "the conversation": nobody can read it.

## Architecture

CarusOS splits work across the two cores of the ESP32-S3:

- **Core 1 — UI**: the `Task_GUI` loop in `CarusOS_ESP32S3.ino` calls
  `lv_task_handler()` and flushes. `ui_core.cpp` is the shell (boot screen,
  desktop, app registry, window chrome, launcher); each app lives in `src/apps/`.
- **Core 0 — Backend** (`backend_core.cpp`): WiFi, NTP, RTC, audio (I2S/ES8311),
  filesystem, OTA and the Telnet log server, all non-blocking.

The two cores talk only through `backend_core.h`:

- **UI → backend actions** that touch WiFi/OTA/Telnet/BLE/NVS/FS hardware go
  through a FreeRTOS command queue drained by `Task_Backend` on Core 0.
- **Backend → UI state** (WiFi connected, audio volume, FS results…) is exposed
  as `volatile` flags read through getters; the UI polls them with `lv_timer`s.
- **Logs** (`backend_log()`, also LVGL's log callback) go through a queue; only
  Core 0 touches the Telnet socket.

Never call LVGL from Core 0, and never do slow hardware/network work inside an
LVGL callback.

### File map (sketch folder `ESP32-S3/CarusOS_ESP32S3/`)

| File | Responsibility |
|------|----------------|
| `CarusOS_ESP32S3.ino` | Entry point; creates the Core 1 GUI task |
| `carusos_config.h` | Master feature flags + build-time sanity checks (`#error`) |
| `customizations/secrets.h` | WiFi/OTA credentials — **gitignored**, create from `secrets.h.example`. Included only by `backend_core.cpp` |
| `customizations/config_local.h` | Personal, non-secret build config (URLs…) — **gitignored**, create from `config_local.h.example` |
| `pin_config.h` | All pins: audio codec, shared I2C bus, I/O-expander channels |
| `ui_core.cpp` | Core 1 shell: boot, desktop, app registry, window chrome, status bars |
| `src/apps/apps.h` | App IDs, builder declarations, shell helpers for apps |
| `src/apps/app_*.cpp` | One file per app (or per group of small apps) |
| `backend_core.cpp` | Core 0: WiFi, NTP, RTC sync, audio, FS, OTA, Telnet |
| `lvgl_port.cpp` | Display/touch/LVGL hardware glue (panel, GT911, backlight, expander) |
| `language.h` | Compile-time i18n macros (`TXT_*`), EN/ES |
| `es8311.*`, `audio_hal.h` | Speaker codec driver |
| `es7210.*`, `mic_es7210.*` | Microphone ADC driver + capture task (Mic Test app) |
| `imu_qmi8658.*`, `rtc_pcf85063.*` | Thin wrappers over SensorLib for the IMU and RTC |
| `pmu_axp2101.*` | Read-only AXP2101 wrapper (XPowersLib): battery %, USB, charging. Never writes rails/charger |
| `lv_conf.h` | LVGL configuration |

Files under `src/` include sketch-root headers with relative paths
(`../../carusos_config.h`): Arduino compiles `src/` recursively but does not put
the sketch root on its include path.

## How to add an app

1. Write the builder in `src/apps/` (new file `app_myapp.cpp`, or an existing
   group file for something tiny):
   `void app_build_myapp(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label)`.
   The title bar, back button and status bar already exist.
2. Declare it in `src/apps/apps.h`, add an id to `carusos_app_id_t`.
3. Add one row to `g_apps[]` in `ui_core.cpp` and an `ui_add_app_icon(parent, id)`
   call in a menu (desktop in `create_main_screen()`, or the Settings grid).

**Everything the app creates must die with its screen.** No globals pointing at
LVGL objects. Use the helpers from `apps.h`:

- `ui_screen_timer(screen, cb, ms, user_data)` — timer deleted with the screen.
- `ui_screen_alloc(screen, size)` — zeroed state struct freed with the screen.
- Anything else: `lv_obj_add_event_cb(screen, cleanup, LV_EVENT_DELETE, state)`.

The Back button loads the previous screen with `auto_del`, so LVGL deletes the
app screen after the transition and runs those `LV_EVENT_DELETE` handlers. Don't
free anything in a click handler: the screen is still drawn during the fade.
Memory handed to a Core 0 task uses plain `malloc` with an explicit ownership
handoff (see `app_gallery.cpp`).

### Optional apps / features

- Gate the **code** (builder, drivers, backend paths) with its flag, so a
  disabled feature compiles out completely (zero flash/RAM).
- Keep the **registry row and the `ui_add_app_icon()` call** unconditional, with
  `enabled = FLAG` and the builder replaced by `NULL` via the `BUILD_*` macros at
  the top of `ui_core.cpp`. That's a few bytes, and it lets
  `CARUSOS_SHOW_DISABLED_APPS 1` show the app greyed out.
- If a flag depends on another one, add an `#error` check at the bottom of
  `carusos_config.h` rather than shipping an app that only says "disabled".

Apps with a `NULL` builder that are *enabled* are "actions" handled in
`app_launcher_event_cb` (e.g. Sleep).

## Versioning

`CARUSOS_VERSION` in `carusos_config.h` is the single source of truth, format
`vMAJOR.MINOR.PATCH`:

- **Bug fix** → bump the **PATCH** (last) digit.
- **New feature** → bump the **MINOR** (middle) digit.

When bumping, also update the README version badge and set `CARUSOS_SOURCE_DATE`
to the current date.

## Build & flash

Arduino IDE (sketch folder `ESP32-S3/CarusOS_ESP32S3/`). Before the first build,
copy `customizations/secrets.h.example` to `customizations/secrets.h` and
`customizations/config_local.h.example` to `customizations/config_local.h`,
then fill in your values.

Critical Arduino IDE settings (see README for the full list): Board
`ESP32S3 Dev Module`, Flash `16MB`, Partition `16M Flash (3MB APP / 9.9MB FATFS)`,
PSRAM `OPI PSRAM`, Flash Mode `QIO 80MHz`. Wrong settings cause bootloops or a
very slow UI.

Dependencies (pinned in CI): ESP32 Arduino core 3.3.10, LVGL 9.5.0, GFX Library
for Arduino 1.6.6, SensorLib 0.4.1, XPowersLib 0.3.3. ESP_I2S ships with the core. GFX must be
>= 1.6.5: older releases call the pre-3.3 `spiFrequencyToClockDiv()` signature and
fail to compile on core 3.3.x.

LVGL reads `lv_conf.h` from the Arduino `libraries/` folder, **not** from the
sketch. After editing the sketch's `lv_conf.h`, copy it there (CI does the same
copy); a stale copy silently builds with different settings.

A local compile check with `arduino-cli` (the same FQBN as CI) catches build
breaks, but only the hardware tells you whether it works. Land changes in small
steps the owner can flash-test one at a time.

**Before a risky flash** (enabling the File System, erasing regions, OTA
experiments), make sure there is a full flash backup of the working board, and
know how to restore it: README → *Backup & Recovery*. Never erase flash without
asking the owner first. Backups contain the WiFi credentials: they live on the
owner's machine, never in the repo.

## How this "deploys"

CarusOS has **no website and no release pipeline**. It reaches people and
hardware in three ways, none of them automatic:

1. **To people → the public repo itself.** `github.com/jdcaruso/CarusOS` is the
   only storefront: the `README.md` **is** the project's landing page. There is
   no domain and no GitHub Pages. Changing the README **is** publishing.
2. **To the hardware, by cable →** Arduino IDE with the critical settings above.
   Always manual.
3. **To the hardware, over the air →** OTA, when `CARUSOS_USE_OTA` is on; the
   password comes from `customizations/secrets.h`. Also manual.

**No published releases or attached binaries** (`gh release list` is empty) —
whoever wants CarusOS builds it. If binaries are ever published, write that
decision down here.

### CI: there is one, it's cheap — leave it alone

[`.github/workflows/build.yml`](.github/workflows/build.yml) is a **compile check
with `arduino-cli`**: it generates dummy `secrets.h`/`config_local.h` from the
`*.example` files and does a real compile. **It flashes nothing and publishes
nothing.**

- Runs on `ubuntu-latest` (~4 min), only on push to `main`/`dev` **and only when
  `ESP32-S3/**` or the workflow itself changed**. Docs-only edits trigger nothing.
- **Actions is ON in this repo on purpose.** Linux bills 1x and the cost is
  marginal. Before adding, widening or "improving" any workflow here, read the
  «GitHub Actions» section of the owner's `~/Code/adm/CLAUDE.md`: a macOS/Windows
  runner once ate a month's quota in two days.

### Branches

`main` and `dev`, both with CI. `main` is the default and what anyone visiting
the repo sees — README changes only count once they're on `main`.

## Conventions

- Feature flags live in `carusos_config.h`; gate optional code with `#if CARUSOS_USE_*`
  / `ENABLE_APP_*`. No phantom flags: a flag that nothing reads gets removed.
- **Keep it small.** The Arduino IDE prints `Sketch uses N bytes (X%)` after each
  build — watch the % on the 3 MB app partition.
- All pins go in `pin_config.h`, defined once.
- User-facing strings go through `TXT_*` macros in `language.h` (keep EN + ES in
  sync), including text the backend builds for the UI.
- Code, comments and docs are in English.
- Never commit credentials. `customizations/secrets.h` is gitignored; only
  `customizations/secrets.h.example` is tracked. Same pattern for non-secret
  personal build config in `customizations/config_local.h` /
  `config_local.h.example`. Any new personal override file follows this pattern:
  real file gitignored, `*.example` with placeholder values tracked, and the CI
  step that generates the dummy copy.
- The repo is public — no real SSIDs, passwords, private URLs, or personal data
  in tracked files.

## Known limitations (do not "fix" silently — they are intentional/known)

- Telnet log server (port 23) is unauthenticated.
- HTTPS downloads use `setInsecure()` (no TLS cert validation).
- Backend → UI state uses `volatile` flags. Fine for single-word flags written by
  one side; anything bigger goes through a queue or a ready-flag handoff like
  `backend_fs_poll()`.
- After using the Mic Test app the speaker stays off until reboot (the mic takes
  over the shared I2S clock pins). See `pendings.md`.
