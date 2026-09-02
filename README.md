# MatrixFaces

[![CI](https://github.com/mrquentin/MatrixFaces/actions/workflows/ci.yml/badge.svg)](https://github.com/mrquentin/MatrixFaces/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mrquentin/MatrixFaces)](https://github.com/mrquentin/MatrixFaces/releases/latest)
[![License](https://img.shields.io/github/license/mrquentin/MatrixFaces)](LICENSE)
[![Last commit](https://img.shields.io/github/last-commit/mrquentin/MatrixFaces)](https://github.com/mrquentin/MatrixFaces/commits/master)
[![PlatformIO](https://img.shields.io/badge/platformio-orange?logo=platformio&logoColor=white)](https://platformio.org)

> **🚧 Migration in progress — Matrix Portal M4 → MatrixPortal S3.** This repo is
> being reworked into a multi-board build where both targets ship from one tree,
> picking up a FreeRTOS task split, WebSocket settings, MQTT/Home Assistant
> discovery, OTA, mDNS, captive-portal WiFi provisioning and an on-device web UI
> along the way. The M4 keeps working throughout. Expect churn in the layout,
> the build environments and these docs until the migration lands; see
> [docs/migration-context.md](docs/migration-context.md) for the background.

A modular Adafruit Matrix Portal M4 display: swap between "apps" on one RGB
LED matrix — a clock, an F1 race-flag indicator, scrolling text, and whatever
comes next.

This repo ships a signed HTTP API on the Matrix Portal M4, with button-gated
pairing and HMAC-SHA256 request signing, plus a swappable-app framework
(`App`/`AppScheduler`) driving the RGB matrix — see [Roadmap](#roadmap) below.

## Quick start

    pio run -e m4 -t upload
    pio device monitor

Copy `include/secrets.h.example` to `include/secrets.h` and fill in your WiFi
credentials first (see [Development](docs/development.md) for the full
build/test/versioning workflow). Press **UP** on the board to open a 60s
pairing window, then pair with `tools/m4client.py` — see [Client](docs/client.md).

## Roadmap

- [x] Matrix rendering framework — swappable "apps" driving the RGB panel
- [x] Clock app, text app
- [x] F1 race-flag app (polls a local MultiViewer GraphQL API)
- [x] `/api/apps` and `/api/app` endpoints to list, query, and switch the active app
- [ ] MatrixPortal S3 support (see the migration banner above)
- [ ] Weather app

## Documentation

- [Security model](docs/security.md) — why there's no HTTPS, the pairing flow,
  HMAC request signing and replay defense
- [API reference](docs/api.md) — endpoint table
- [Client](docs/client.md) — using `tools/m4client.py`
- [Where credentials and settings live](docs/flash-storage.md) — the flash
  storage design and why the addresses are pinned
- [Metrics](docs/metrics.md) — CPU/RAM instrumentation, `/api/metrics`
- [Development](docs/development.md) — build, flash, test, versioning, releases
