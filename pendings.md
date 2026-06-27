# CarusOS — Pending Tasks

Short-term, working list of deferred / "do it later" tasks. Strategic feature
direction lives in [ROADMAP.md](ROADMAP.md); this is the scratch list of concrete
TODOs captured during sessions.

## 📝 To do

- [ ] **Show the CarusOS version on the boot/splash screen.** Trivial and *not*
  error-prone: `CARUSOS_VERSION` is a compile-time string macro from
  `carusos_config.h`, already included everywhere — no runtime file loading needed.
  Just render it on the boot/splash screen (boot code in `ui_core.cpp`).
  *Deferred 2026-06-27.*

- [ ] **Measure real per-feature flash cost (differential builds).** The README has a
  *source-size* table (real, but only the in-sketch driver files); add a complementary
  **measured flash-cost** table: build with a flag ON vs OFF and record the delta from
  the Arduino IDE's `Sketch uses N bytes`. This captures the heavy ones that have no
  in-sketch file (WiFi, OTA, FFat, BLE ≈ 1 MB). *Deferred 2026-06-27.*

## 🐞 Known bugs

- [ ] **File Explorer crash.** Enabling `CARUSOS_USE_FS=1` + `CARUSOS_APP_EXPLORER=1`
  stops the device from booting — Core-1 null deref (EXCCAUSE 5, EXCVADDR 0,
  PC `0x4037ae82`) **at boot**, before the app is ever opened. Disabled behind flags
  for now. Next step: capture the boot Serial (`[NVS]`/`[Backend]`/`[UI]` markers)
  right before the panic, and bisect with `FS=1 / EXPLORER=0`. Debug continues on the
  `dev` branch.
