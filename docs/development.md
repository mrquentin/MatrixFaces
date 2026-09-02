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

## Editor setup (clangd)

`compile_commands.json` is machine-local and gitignored; regenerate it whenever
include paths change, scoped to the board env:

    pio run -e adafruit_matrix_portal_m4 -t compiledb

Always pass `-e`. A bare `pio run -t compiledb` walks every environment in
declaration order and *overwrites* the file each pass instead of merging, so
`env:native`'s bare-host command lines end up clobbering the firmware include
paths and clangd loses every Arduino/WiFiNINA/Protomatter header.

`.clangd` points at `arm-none-eabi-g++` by name so clangd queries the cross
toolchain (not the host) for system headers like `<cstdint>`. Put the
PlatformIO toolchain on your PATH so that name resolves:

    ~/.platformio/packages/toolchain-gccarmnoneeabi/bin

## Dependency pinning

`lib_deps` pins exact versions: the Adafruit WiFiNINA fork publishes no
releases, so it is pinned to a commit hash — without it a fresh `.pio` silently
picks up whatever GitHub HEAD is that day. Bumping a dependency is a deliberate
edit to `platformio.ini`, verified by a board build.

## Versioning

Each build bakes `git describe --tags --always --dirty` into `FIRMWARE_VERSION`,
reported by `GET /` and printed on boot — so a flashed board can be checked
against what's in git. Tags follow `vMAJOR.MINOR.PATCH`; pushing a `v*` tag
triggers a GitHub Release (`.github/workflows/release.yml`) with the board and
release firmware `.bin` files attached.
