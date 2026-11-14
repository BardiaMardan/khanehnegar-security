# Changelog

All notable changes to this project are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Full documentation: architecture, hardware, protocol, API, configuration, security model, deployment, and step-by-step tutorials.
- Persian README.
- MIT license, contributing guide, security policy, and issue/PR templates.
- CI that compiles all three sketches and builds the dashboard.

### Changed
- ESP-NOW keys moved out of `SecMeshConfig.h` into a per-site, git-ignored `SecMeshKeys.h`. When that file is missing, the build falls back to the example keys and prints a warning.
- Door node touch tracing is off by default.
- The dashboard bundles the Vazirmatn font instead of fetching it from Google Fonts at build time, so it builds offline and where Google is blocked.
- The SD-card examples no longer mention the duress finger, which the hub dropped earlier.

### Removed
- `sdcard-example/khaneh.zip`, which duplicated the `khaneh/` folder.

## [0.1.0] - 2026-09-07

First working system, built between 2026-08-30 and 2026-09-07.

### Hub
- ESP32 terminal with ILI9341 touch screen: STATUS, CODE, LOG, and MANAGE pages, a boot splash, and a standby clock.
- Risk-fusion alarm engine with separate loud and silent evidence, entry and exit delays, and an armed state that survives power cuts.
- Salted SHA-256 keypad code with a constant-time compare, and a lockdown latch after 5 wrong codes that survives reboots.
- R308 fingerprint reader: enrol, rename, remove, and an unknown-finger limit.
- Event journal in NVS with a CSV archive on microSD. Site config comes from the SD card.
- A network task pinned to its own core, so a slow server never stalls the touch panel.
- Web actions (finger removal, wipe, forget nodes) must be approved on the terminal.

### SecMesh
- Encrypted ESP-NOW (PMK/LMK) with a 15-byte frame, replay counter, broadcast pairing handshake, heartbeat answered by the hub, a 13 s dead-node timeout, and channel hunting.
- Plaintext fallback for ESP8266 nodes.

### Nodes
- Door node: ADXL345 movement, impact, and forcing detection, plus the ESP32 capacitive touch pad as a silent handle trigger.
- Laser tripwire on a LilyGO T-Display: strobed beam that cancels ambient light, and a case tamper switch.

### Dashboard
- Next.js 15 dashboard in Persian and English (RTL), with better-auth login, an event feed, remote arm/disarm/panic/lock/unlock, and fingerprint management.
- Telegram and Bale alerts, and bot commands (`/arm`, `/disarm`, `/panic`, `/lock`, `/unlock`).

[Unreleased]: https://github.com/BardiaMardan/khanehnegar-security/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/BardiaMardan/khanehnegar-security/releases/tag/v0.1.0
