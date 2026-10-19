# CLAUDE.md

Guidance for AI assistants (Claude, Gemini, etc.) working in this repository.

## What CarusOS is

A lightweight, multi-threaded mini-OS for the **ESP32-S3** with an RGB touch
display, built on **LVGL 9.x**. Target hardware: **Waveshare ESP32-S3-Touch-LCD-4B**
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
| `customizations/secrets.h` | WiFi/OTA credentials — **gitignored**, create from `customizations/secrets.h.example` |
| `customizations/config_local.h` | Personal, non-secret build config (URLs, hostnames…) — **gitignored**, create from `customizations/config_local.h.example` |
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
copy `customizations/secrets.h.example` to `customizations/secrets.h` and
`customizations/config_local.h.example` to `customizations/config_local.h`,
then fill in your values.

Critical Arduino IDE settings (see README for the full list): Board
`ESP32S3 Dev Module`, Flash `16MB`, Partition `16M Flash (3MB APP / 9.9MB FATFS)`,
PSRAM `OPI PSRAM`, Flash Mode `QIO 80MHz`. Wrong settings cause bootloops or a
very slow UI.

Dependencies: LVGL 9.x, Arduino_GFX, SensorLib (GT911), ESP_I2S.

## How this "deploys" (o sea: cómo llega a algún lado)

CarusOS **no tiene sitio web ni pipeline de release**. Escrito acá porque la pregunta
«¿cómo deploya esto?» no se contesta sola y la respuesta correcta es «de tres maneras
distintas, ninguna automática»:

1. **A la gente → el repo público mismo.** `github.com/jdcaruso/CarusOS` es la única
   vitrina: el `README.md` **es** la landing del proyecto (badges, features, settings
   de compilación). No hay dominio, no hay GitHub Pages, y `homepageUrl` del repo está
   vacío. Cambiar el README **es** publicar.
2. **Al hardware, por cable →** Arduino IDE sobre `ESP32-S3/CarusOS_ESP32S3/`, con los
   settings críticos del README (16MB / `16M Flash (3MB APP / 9.9MB FATFS)` / OPI PSRAM
   / QIO 80MHz). Paso manual, siempre.
3. **Al hardware, por aire →** OTA, si `CARUSOS_USE_OTA` está prendido; la password sale
   de `customizations/secrets.h` (gitignored). También manual.

**No hay releases publicadas ni binarios adjuntos** (`gh release list` está vacío) — quien
quiera CarusOS lo compila. Si algún día se publican binarios, esa decisión se escribe acá.

### CI: sí hay, y es barato — no tocarlo

[`.github/workflows/build.yml`](.github/workflows/build.yml) hace un **compile-check con
`arduino-cli`**: genera `secrets.h`/`config_local.h` dummy desde los `*.example` y compila
de verdad. **No flashea nada y no publica nada** — es un gate de "no rompiste el build",
no un deploy.

- Corre en `ubuntu-latest` (~4 min), sólo en push a `main`/`dev` **y sólo si tocaste
  `ESP32-S3/**` o el propio workflow**. Editar README/docs no dispara nada.
- **La flag de Actions está PRENDIDA en este repo, a propósito** — a diferencia de
  `wine-and-sorcery`, donde está apagada. Linux es 1x y el gasto es marginal. Antes de
  agregar, ampliar o "mejorar" cualquier workflow acá, leé la sección «GitHub Actions» de
  `~/Code/adm/CLAUDE.md`: hay un costo medido detrás de esa regla (un runner de macOS/Windows
  se comió la cuota del mes en dos días).

### Ramas

`main` y `dev`, las dos con CI. `main` es la default y es la que ve cualquiera que entre al
repo — o sea, lo que toque el README sólo cuenta cuando está en `main`.

## Conventions

- Feature flags live in `carusos_config.h`; gate optional code with `#if CARUSOS_USE_*`.
- **Keep it small.** A disabled feature must compile out *completely* (zero
  flash/RAM). For an app, gate all three with the same `#if`: the builder
  function, the `g_apps[]` row, and the menu `add_app_icon()` call (see the
  Animation / Mic Test apps). The Arduino IDE prints `Sketch uses N bytes (X%)`
  after each build — watch the % on the 3 MB app partition.
- User-facing strings go through `TXT_*` macros in `language.h` (keep EN + ES in sync).
- Never commit credentials. `customizations/secrets.h` is gitignored; only
  `customizations/secrets.h.example` is tracked. Same pattern for non-secret
  personal build config (personal URLs, hostnames…) in
  `customizations/config_local.h` / `config_local.h.example`. Any new personal
  override file follows this pattern: real file gitignored, `*.example` with
  placeholder values tracked and referenced from `carusos_config.h`.
- The repo is public — no real SSIDs, passwords, private URLs, or personal data
  in tracked files.

## Known limitations (do not "fix" silently — they are intentional/known)

- Telnet log server (port 23) is unauthenticated.
- HTTPS downloads use `setInsecure()` (no TLS cert validation).
- Cross-core state uses `volatile` flags (fine for a few flags; a FreeRTOS
  queue is the planned upgrade — see ROADMAP).
