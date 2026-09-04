# Development

## Build, flash, and test

    pio run -e m4 -t upload
    pio run -e s3 -t upload

    pio device monitor --port COM7     # see "Two boards on one USB bus" below

    pio test -e native      # host tests; needs gcc/g++ on PATH

## Build environments

| Env | What it is |
|---|---|
| `m4` | Matrix Portal M4: clock, text and F1 apps |
| `m4_release` | `m4` with instrumentation compiled out (no `/api/metrics`) |
| `s3` | MatrixPortal S3: the same apps |
| `s3_release` | `s3` with instrumentation compiled out |
| `native` | Host tests |

Three rules keep the boards apart, and none of them is `#ifdef`:

- **Sources** are selected by `build_src_filter`. A board's directory under
  `src/board/` is either compiled or it is not.
- **Headers** are found via `-Isrc/board/<target>`, so shared code writes
  `#include "board_pins.h"` and the build decides whose.
- **Apps** come from exactly one file under `src/variants/`, which is also
  where their dependencies are constructed. A build that drops an app drops its
  buffers too: the 32 KB MultiViewer response buffer lives next to the F1 app,
  not in `main.cpp`, so it is not something every build has to carry.

There is one variant today, so the filter simply takes the whole directory; a
second would move the selection into the individual environments. A missing or
duplicated `registerApps()` is a link error, which is the intended safety net
for a filter mistake.

Where the boards genuinely differ in *capability* rather than in
implementation, `board_caps.h` says so as `constexpr bool`, and shared code
branches on that. The unreachable side still compiles and is still type-checked,
which an `#ifdef` would not give you: `ClockApp` builds its timezone setting on
both boards and only counts it into the bag where `kHasPosixTz` is true.

## Two boards on one USB bus

Both boards enumerate under Adafruit's vendor ID `239A`, so "the Adafruit
device on the bus" is not enough to tell them apart:

| Board | VID:PID | Notes |
|---|---|---|
| Matrix Portal M4 | `239A:80C9` | `239A:00C9` in the SAM-BA bootloader |
| MatrixPortal S3 | `239A:8125` | `303A:1001` in ROM download mode |

`pio device list` prints what is actually attached. PlatformIO prefers a port
matching the board being built, but when that board is *absent* it falls back
to the first USB serial device it can find — which is how an M4 upload ends up
pointed at the S3. `tools/pin_upload_port.py` runs before every upload, resolves
the port from the board's own VID:PID, and stops the build with the list of
attached ports instead of guessing. Pass `--upload-port` to override it.

`pio device monitor` does not run build scripts and so cannot do the same;
give it `--port` explicitly when both boards are plugged in.

## Reading the S3's boot log

The S3 speaks USB straight from the chip, so an upload takes its serial port
off the bus: the 1200-bps touch reboots it into ROM download mode under a
different USB ID, and the application port only returns after the reset at the
end of the upload. A monitor opened beforehand loses its port; one opened
afterwards has already missed `setup()`. `tools/boot_log.py` waits the gap out:

    python tools/boot_log.py 90 --reboot &
    pio run -e s3 -t upload

Opening the port is also what satisfies the firmware's five-second wait for a
serial monitor at the top of `setup()`, so nothing is lost. The M4 keeps its
port across an upload, so `pio device monitor` is fine there.

That 1200-bps touch is occasionally flaky: the board can reboot into download
mode and fail to re-enumerate, dropping off USB *and* the network. Tap RESET,
or unplug and replug. It fails before writing anything, so the image in flash
is whatever was last uploaded successfully.

## Flashing the S3

`pio run -e s3 -t upload` writes the bootloader, the partition table and the
application. The release workflow publishes the application image alone, which
is what phase 6.3's OTA route will consume — it is not enough on its own for a
blank board.

The `s3` environments replace the board's stock partition table
(`boards/partitions_8mb_ota.csv`) because the stock one spends the second OTA
slot on Adafruit's UF2 bootloader, leaving no room for an A/B update. **That
means no UF2 drive**: a bad image is recovered by holding **BOOT** while tapping
**RESET**, which puts the chip's ROM download mode on the USB port, and then
uploading normally. The ROM loader is in mask ROM and cannot be bricked.

Changing that partition table erases NVS, and NVS is where paired credentials
and app settings live — so it is fixed, and everything phases 5 and 6 need is
already reserved in it.

## The on-device web UI's filesystem

`data/` (`index.html`, `app.js`, `style.css`) is compiled into a LittleFS
image and flashed separately from the firmware:

    pio run -e s3 -t buildfs      # compile data/ -> .pio/build/s3/littlefs.bin
    pio run -e s3 -t uploadfs     # flash it

It is not part of `-t upload` and not part of OTA: the partition holding it
(`boards/partitions_8mb_ota.csv`) only needs writing once, or again after a
change to `data/`. `board_caps::kHasFilesystem` gates serving it (true on the
S3, false on the M4, which keeps its one embedded page); a board that
answers false never mounts or reads it.

### Host test suites

`env:native` builds the portable half of the firmware (see `build_src_filter`
in `platformio.ini`) against the Arduino fakes in `test/support/fake_arduino/`:

| Suite | Covers |
|---|---|
| `test_apiauth` | SHA-256 and HMAC against FIPS 180-4 / RFC 4231 vectors, signing, header parsing |
| `test_http_request` | request parsing, size caps, and every timeout path |
| `test_router` | route matching, wildcards, and which paths 404 |
| `test_json` | body parsing, response serialization, Content-Length |
| `test_multiviewer_parse` | the MultiViewer parser against captured responses |
| `test_settings` | `SettingsBag` types, ranges, string caps, unknown keys |
| `test_blob_store` | the stored blob format, byte-for-byte, against goldens |

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
`SECRET_SSID`/`SECRET_PASS`; the real file is gitignored. Both boards read it
today. Phase 6.1 gives the S3 a captive portal and NVS-stored credentials, at
which point it becomes the M4's alone.

CI (`.github/workflows/ci.yml`) runs the native tests plus a compile-only check
of every board environment on every push — it never touches real hardware.

## Editor setup (clangd)

`compile_commands.json` is machine-local and gitignored; regenerate it whenever
include paths change, scoped to the board env:

    pio run -e m4 -t compiledb

Always pass `-e`. A bare `pio run -t compiledb` walks every environment in
declaration order and *overwrites* the file each pass instead of merging, so
`env:native`'s bare-host command lines end up clobbering the firmware include
paths and clangd loses every Arduino/WiFiNINA/Protomatter header. With two
boards this also decides which `board_pins.h` and `board_caps.h` clangd
resolves, so regenerate after switching the board you are working on.

`.clangd` points at `arm-none-eabi-g++` by name so clangd queries the cross
toolchain (not the host) for system headers like `<cstdint>`. Put the
PlatformIO toolchain on your PATH so that name resolves:

    ~/.platformio/packages/toolchain-gccarmnoneeabi/bin

## Dependency pinning

`lib_deps` pins exact versions: the Adafruit WiFiNINA fork publishes no
releases, so it is pinned to a commit hash — without it a fresh `.pio` silently
picks up whatever GitHub HEAD is that day. The `espressif32` platform is pinned
for the same reason: it decides the Arduino ESP32 core version (7.0.1 →
core 2.0.17), and a floating platform would move the whole API under the S3
sources without anything in this repo changing. Bumping either is a deliberate
edit to `platformio.ini`, verified by a board build.

## Versioning

Each build bakes `git describe --tags --always --dirty` into `FIRMWARE_VERSION`,
reported by `GET /` and printed on boot — so a flashed board can be checked
against what's in git. Tags follow `vMAJOR.MINOR.PATCH`; pushing a `v*` tag
triggers a GitHub Release (`.github/workflows/release.yml`) with the board and
release firmware `.bin` files attached.
