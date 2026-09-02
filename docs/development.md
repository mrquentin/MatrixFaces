# Development

## Build, flash, and test

    pio run -e adafruit_matrix_portal_m4 -t upload
    pio device monitor

    pio test -e adafruit_matrix_portal_m4    # runs on the board over serial
    pio test -e native                       # needs a host g++/clang++ on PATH

### Host test suites

`env:native` builds the portable half of the firmware (see `build_src_filter`
in `platformio.ini`) against the Arduino fakes in `test/support/fake_arduino/`:

| Suite | Covers |
|---|---|
| `test_apiauth` | SHA-256 and HMAC against FIPS 180-4 / RFC 4231 vectors, signing, header parsing |
| `test_http_request` | request parsing, size caps, and every timeout path |
| `test_router` | route matching, wildcards, and which paths 404 |
| `test_json` | body parsing, response serialization, Content-Length |

The fakes make `millis()` a controlled value that `delay()` advances rather
than real time, so the slow-client cases — a trickled header, a stalled body —
run deterministically in microseconds instead of needing a four-second wall
clock and a real socket.

`test_router`'s expectations were captured from a running board *before* the
router was extracted from `main.cpp`, which is what makes it a regression check
on that extraction rather than a description of whatever the code now does.

Note that PlatformIO's native platform invokes `gcc`/`g++` by name and ignores
`CC`/`CXX`, so those must be on PATH. On a machine with only clang, shim them:
put `g++.cmd`/`gcc.cmd` (plus `ar`/`ranlib` → `llvm-ar`/`llvm-ranlib`) somewhere
on PATH forwarding to the clang equivalents.

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
