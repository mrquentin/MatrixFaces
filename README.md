# MatrixFaces

[![CI](https://github.com/mrquentin/MatrixFaces/actions/workflows/ci.yml/badge.svg)](https://github.com/mrquentin/MatrixFaces/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mrquentin/MatrixFaces)](https://github.com/mrquentin/MatrixFaces/releases/latest)
[![License](https://img.shields.io/github/license/mrquentin/MatrixFaces)](LICENSE)
[![Last commit](https://img.shields.io/github/last-commit/mrquentin/MatrixFaces)](https://github.com/mrquentin/MatrixFaces/commits/master)
[![PlatformIO](https://img.shields.io/badge/platformio-orange?logo=platformio&logoColor=white)](https://platformio.org)

A modular Adafruit Matrix Portal M4 display: swap between "apps" on one RGB
LED matrix — weather, an F1 race-flag indicator, a clock, and whatever comes
next.

This repo ships a signed HTTP API on the Matrix Portal M4, with button-gated
pairing and HMAC-SHA256 request signing, plus a swappable-app framework
(`App`/`AppScheduler`) driving the RGB matrix. The clock app is the first
renderer; weather and F1 are not built yet — see [Roadmap](#roadmap) below.

## Quick start

    pio run -e adafruit_matrix_portal_m4 -t upload
    pio device monitor

Copy `include/secrets.h.example` to `include/secrets.h` and fill in your WiFi
credentials first (see [Development](docs/development.md) for the full
build/test/versioning workflow). Press **UP** on the board to open a 60s
pairing window, then pair with `tools/m4client.py` — see [Client](docs/client.md).

## Roadmap

The signed API above is the control plane; the display side is next.

- [x] Matrix rendering framework — swappable "apps" driving the RGB panel
- [x] Clock app
- [ ] Weather app
- [ ] F1 race-flag app
- [x] `/api/apps` and `/api/app` endpoints to list, query, and switch the active app

## Documentation

- [Security model](docs/security.md) — why there's no HTTPS, the pairing flow,
  HMAC request signing and replay defense
- [API reference](docs/api.md) — endpoint table
- [Client](docs/client.md) — using `tools/m4client.py`
- [Where credentials live](docs/flash-storage.md) — the flash storage design
  and why the address is pinned
- [Metrics](docs/metrics.md) — CPU/RAM instrumentation, `/api/metrics`
- [Development](docs/development.md) — build, flash, test, versioning, releases
