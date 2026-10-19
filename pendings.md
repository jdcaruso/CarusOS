# CarusOS — Pending Tasks

Short-term working list: concrete TODOs and **known bugs, with their current
status**. This is the one place for bug status; strategic direction lives in
[ROADMAP.md](ROADMAP.md). When a session learns something about a bug (root
cause, workaround, ruled-out hypothesis), it goes into the entry below.

## 🔬 Needs a hardware test

- [ ] **v0.47.0 (backlight on GPIO4 + deep sleep).** Flashed 2026-10-19; boots,
  touch controller found, serial log OK. Only visible on the screen:
  - The screen lights up at boot (backlight now on GPIO4 PWM, active high).
  - **Sleep** (Settings) turns the screen **off**, and a touch turns it back on.
    Before this release Sleep pulsed the touch reset (expander pin 5) instead.
  - **Deep sleep** (Settings, eject icon): "press BOOT" message, screen goes
    dark after 2 s, and stays dark (GPIO4 is held low). Pressing **BOOT** reboots
    it; the serial log shows `[Power] Woke from deep sleep (BOOT button).`
  - If the screen stays black at boot: the backlight polarity guess is wrong
    (`ledcWrite(PIN_LCD_BL, 255)` in `lvgl_port_init()`).
- [ ] **v0.46.0 (AXP2101 battery status).** Flashed 2026-10-19; serial log shows
  `[PMU] AXP2101 ready.` and `battery=no, usb=1` (board on USB, no battery).
  To check: the USB icon left of the WiFi icon (desktop and app title bars), the
  Battery line in Sys Info, and **with a LiPo connected** that % / mV look sane
  and the bolt shows while charging. The battery path is untested.
- [ ] **v0.45.0 (library update + more apps).** Flashed 2026-10-19 by cable; boot,
  WiFi, NTP and the RTC checked on the serial log only (nobody looked at the
  screen). Built with LVGL 9.5.0, SensorLib 0.4.1, Arduino_GFX 1.6.6 (stock, the
  hand-patched 1.6.0 is gone). What to check on the device:
  - Display and touch behave as before (GFX and SensorLib's GT911 driver changed).
  - **Animation** app (new on the desktop) and **Mic Test** (Settings grid) open
    and Back works.
  - Gallery still shows the PNG (LVGL 9.5; `lv_image_cache_drop` is now declared
    by hand in `app_gallery.cpp`, see the comment there).
  - Known: after Mic Test the speaker stays dead until reboot (bug below).
- [ ] **v0.44.1 (code-review cleanup).** Flashed and boots; the UI checks were never done (they still apply to v0.45.0). What to
  check on the device:
  - Every app opens and **Back** works, with UI animations both on and off.
  - **Gallery:** leave while the image is still downloading, and again after it
    shows. No crash, and reopening works. (The image buffer used to be freed in
    the Back click handler while the fade-out was still drawing it.)
  - **Nested windows:** Settings → Sys Info → Back. The clock and WiFi icon in
    the Settings title bar must keep updating (they used to freeze).
  - Date & Time and Sys Info refresh live; the Motion app ball moves.
  - Options: the WiFi / Telnet / Animations switches still persist across reboots.

## 📝 To do

- [ ] **Measure real per-feature flash cost (differential builds).** The README has a
  *source-size* table (real, but only the in-sketch driver files); add a complementary
  **measured flash-cost** table: build with a flag ON vs OFF and record the delta from
  the Arduino IDE's `Sketch uses N bytes`. This captures the heavy ones that have no
  in-sketch file (WiFi, OTA, FFat, BLE ≈ 1 MB). *Deferred 2026-06-27.*
  Baseline for v0.44.0 with the default flags: 1,684,455 bytes (53% of 3 MB).
- [ ] **Clear the deprecation warnings from the new libraries.** SensorLib 0.4.1:
  `TouchDrvGT911.hpp` is deprecated (include `TouchDrv.hpp`) and `getPoint()` →
  `getTouchPoints()` (`lvgl_port.cpp`). Core 3.3: `NetworkServer::available()` →
  `accept()` (Telnet, `backend_core.cpp`). They compile today; they'll break on a
  future major.
- [ ] **Remove the FS debug instrumentation** (`[FS] …` prints in `fs_do_list()`,
  `backend_core.cpp`) once the File System bug below is closed.

## 🐞 Known bugs

- [ ] **File System: mounting FFat at boot crashes the device.** `CARUSOS_USE_FS`
  and `CARUSOS_APP_EXPLORER` are 0 until this is fixed. History, oldest first:
  1. *v0.40–v0.43:* with FS=1 the boot looped inside `FFat.begin(false)` in
     `Task_Backend` (Core 0), right after `[Backend] Mounting FFat...`
     (`rst:0xc` = software reset: panic/abort or watchdog, not brownout). The
     partition *was* found (`ffat` label is correct for `app3M_fat9M_16MB`).
     Cause: leftover data from earlier flashes in the FAT region made the
     wear-levelling layer (`wl_mount`) read a bogus config and abort instead of
     failing cleanly. One-time fix applied on the device: erase the FAT region
     with esptool (exact command, tool location and download-mode steps:
     README → *Backup & Recovery*). After that v0.43.0 booted with the FS working.
  2. *2026-07-13 (v0.44.0):* it broke again in a different way. On the **blank**
     (all `0xFF`) partition left by the erase, `find_volume()` trips the
     **interrupt watchdog on Core 0**: wear-levelling calls
     `heap_caps_get_largest_free_block` for every sector it reads, and that walk
     is slow against the large PSRAM heap. FS turned off again.
  - **Next step (proposed, untested):** format once with `FFat.begin(true)` so the
    partition stops being blank, then go back to `begin(false)` at boot. The
    Explorer's *Format* button already does `begin(true)` on Core 0, so: flash
    with FS=1 + Explorer=1, and if the boot mount survives, format from the app.
    If the boot mount itself trips the watchdog, skip the mount when the first
    sector of the partition is all `0xFF` and leave formatting to the button.
  - **Still open regardless:** the boot mount does not degrade gracefully — if
    `wl_mount` aborts, it takes the whole boot down with it.
  - **Diagnostics:** the core has `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` and the
    partition scheme has a `coredump` partition at `0xFF0000`. After a crash:
    `esptool read_flash 0xFF0000 0x10000 core.bin`, then `espcoredump.py` with
    the build's ELF.
  - **Watch for:** `LCD_RGB_ISR_IRAM_SAFE` / `GDMA_ISR_IRAM_SAFE` are not set in
    the core's sdkconfig, so heavy flash writes (FS, OTA) while the RGB panel
    runs may glitch the display. The real fix is a custom sdkconfig, which needs
    PlatformIO (see ROADMAP).

- [x] **RTC (PCF85063) not detected at boot — fixed in v0.45.0, keep an eye on it.**
  Seen on v0.44.1: `[RTC] PCF85063 not found!`, clock only from NTP. The chip is
  there (0x51 ACKs). On v0.45.0 before the fix it failed on 1 boot out of 3, and on
  that boot an I2C scan "found" ~60 devices: the bus was returning garbage. The
  failing probe ran on Core 0 right after `WiFi.begin()`. Wire already serializes
  transactions between cores, so it is not a plain software race. Fix: probe the
  RTC **before** starting WiFi, retry 3× 100 ms apart, and stop passing the pins
  to SensorLib (it re-ran `setPins()`/`begin()` on the already started bus). 6/6
  boots OK after that. `rtc_set()` now returns bool, so `[RTC] Updated from NTP.`
  only prints when it was written. If it ever fails again the log prints a probe
  of 0x51 and a bus scan.

- [ ] **Speaker is dead after using the Mic Test app until reboot.** The mic and
  the speaker share the I2S clock pins; `backend_mic_start()` calls `i2s.end()`
  on the speaker and nothing calls `begin()` again. Also, that `i2s.end()` runs on
  the UI core while `Task_Audio` (Core 0) may be inside `i2s.write()`. Fix: let
  `Task_Audio` own the hand-off (a flag it checks to end/re-init its I2S). **Live
  since v0.45.0**: Mic Test is now enabled, so this one is next in line.
