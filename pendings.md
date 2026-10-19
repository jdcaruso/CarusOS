# CarusOS — Pending Tasks

Short-term working list: concrete TODOs and **known bugs, with their current
status**. This is the one place for bug status; strategic direction lives in
[ROADMAP.md](ROADMAP.md). When a session learns something about a bug (root
cause, workaround, ruled-out hypothesis), it goes into the entry below.

## 🔬 Needs a hardware test

- [ ] **v0.44.1 (code-review cleanup).** Compiles clean; not flashed yet. What to
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
     failing cleanly. One-time fix applied on the device:
     `esptool erase_region 0x610000 0x9E0000`. After that v0.43.0 booted with the
     FS working.
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

- [ ] **Speaker is dead after using the Mic Test app until reboot.** The mic and
  the speaker share the I2S clock pins; `backend_mic_start()` calls `i2s.end()`
  on the speaker and nothing calls `begin()` again. Also, that `i2s.end()` runs on
  the UI core while `Task_Audio` (Core 0) may be inside `i2s.write()`. Fix: let
  `Task_Audio` own the hand-off (a flag it checks to end/re-init its I2S). Low
  priority while `ENABLE_APP_MIC_TEST` is 0.
