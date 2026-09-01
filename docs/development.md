# Development

## Build, flash, and test

    pio run -e adafruit_matrix_portal_m4 -t upload
    pio device monitor

    pio test -e adafruit_matrix_portal_m4    # runs on the board over serial
    pio test -e native                       # needs a host g++/clang++ on PATH

The tests in `test/test_apiauth/` cover SHA-256 and HMAC against FIPS 180-4 and
RFC 4231 vectors, plus signature and header-parsing behaviour. Everything under
`lib/apiauth/` is free of Arduino headers so it builds on the host too.

Copy `include/secrets.h.example` to `include/secrets.h` and fill in
`SECRET_SSID`/`SECRET_PASS`; the real file is gitignored.

CI (`.github/workflows/ci.yml`) runs the native tests plus a compile-only check
of both board environments on every push — it never touches real hardware.

## Versioning

Each build bakes `git describe --tags --always --dirty` into `FIRMWARE_VERSION`,
reported by `GET /` and printed on boot — so a flashed board can be checked
against what's in git. Tags follow `vMAJOR.MINOR.PATCH`; pushing a `v*` tag
triggers a GitHub Release (`.github/workflows/release.yml`) with the board and
release firmware `.bin` files attached.
