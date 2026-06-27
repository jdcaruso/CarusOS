# GEMINI.md

This project's full guidance for AI assistants lives in **[CLAUDE.md](CLAUDE.md)** —
read it first. It covers the architecture, file map, how to add an app, build
instructions, conventions, and known limitations.

Non-negotiables (also in CLAUDE.md):

- **Never commit credentials.** `secrets.h` is gitignored; only `secrets.h.example`
  is tracked. This is a **public** repo — no real SSIDs, passwords, private URLs,
  or personal data in tracked files.
- **Versioning:** `CARUSOS_VERSION` in `carusos_config.h` is the source of truth.
  Bug fix → bump the last digit; new feature → bump the middle digit. Keep the
  README badge and `CARUSOS_SOURCE_DATE` in sync.
