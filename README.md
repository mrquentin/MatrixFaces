# MatrixFaces

[![CI](https://github.com/mrquentin/MatrixFaces/actions/workflows/ci.yml/badge.svg)](https://github.com/mrquentin/MatrixFaces/actions/workflows/ci.yml)

A modular Adafruit Matrix Portal M4 display: swap between "apps" on one RGB
LED matrix — weather, an F1 race-flag indicator, a clock, and whatever comes
next.

This repo currently ships the foundation everything else will build on: a
signed HTTP API on the Matrix Portal M4, with button-gated pairing and
HMAC-SHA256 request signing. Paired clients survive a reboot. The app
framework itself (swappable renderers, a scheduler, the actual weather/F1/clock
apps) is not built yet — see [Roadmap](#roadmap) below.

## Why not HTTPS

The board's network stack lives entirely on the ESP32 co-processor behind SPI.
WiFiNINA exposes `WiFiSSLClient` (TLS *out*) but there is no `WiFiSSLServer`,
and the SAMD51 never sees a raw socket, so it cannot terminate TLS itself.

Instead, every API request is signed. The shared secret never crosses the wire
after pairing — only a signature over the request does — so a passive listener
on the LAN learns nothing reusable.

The one exposure left is the pairing response itself, which carries the secret
in the clear. It is gated by a physical button press, lasts 60 seconds, and is
consumed by the first successful pair. If that residual risk matters, the fix is
an ECDH handshake at pairing so the secret is never transmitted; put the board
behind a TLS-terminating reverse proxy if you need real HTTPS for clients.

## Pairing

1. Press **UP** on the board. The onboard LED blinks for 60 seconds.
2. `POST /pair` within that window. The response contains `client_id` and
   `secret` — this is the only time the secret is ever shown.
3. The window closes on the first successful pair, or after 60 seconds.

Outside the window `/pair` returns `401 {"error":"pairing_closed"}`.

Credentials are written to the SAMD51's emulated EEPROM, so they survive a
reboot. Up to 4 clients can be paired. **Hold DOWN for 5 seconds** to revoke all
of them.

## Signing a request

    Authorization: HMAC id=<16 hex>,ts=<unix seconds>,nonce=<16 hex>,sig=<64 hex>

where `sig` is HMAC-SHA256 over the canonical request, keyed with the raw secret
bytes:

    METHOD \n TARGET \n TS \n NONCE \n SHA256HEX(body)

`TARGET` is the request target exactly as sent, query string included. A request
is rejected unless:

- the timestamp is within 60 seconds of the board's NTP-synced clock,
- the nonce has not been seen recently, and
- the timestamp is not older than the last request served for that client.

The signature is verified *before* any replay state is updated, so an
unauthenticated caller cannot advance a client's high-water mark to lock the
real client out.

Until the board gets NTP time from the WiFi module, API calls return
`503 {"error":"clock_unavailable"}` rather than accepting unbounded replays.

## Endpoints

| Method   | Path                 | Auth | Purpose                          |
| -------- | -------------------- | ---- | -------------------------------- |
| `GET`    | `/`                  | no   | device info, pairing window state |
| `POST`   | `/pair`              | window | issue credentials              |
| `GET`    | `/api/status`        | yes  | uptime, LED, RSSI, clock          |
| `POST`   | `/api/led`           | yes  | `{"on": true}`                    |
| `GET`    | `/api/clients`       | yes  | list paired client ids            |
| `DELETE` | `/api/clients/<id>`  | yes  | revoke a client                   |
| `GET`    | `/api/metrics`       | yes  | CPU and RAM instrumentation       |

## Client

`tools/m4client.py` (standard library only) signs requests for you:

    python tools/m4client.py --host 192.168.1.50 info
    python tools/m4client.py --host 192.168.1.50 pair          # press UP first
    python tools/m4client.py --host 192.168.1.50 get /api/status
    python tools/m4client.py --host 192.168.1.50 post /api/led '{"on": true}'

It stores credentials in `.m4-credentials.json`, which is gitignored.

On Windows, run these from PowerShell or cmd. Git Bash rewrites a leading `/` in
an argument into a Windows path, so `/api/status` arrives as
`C:/Program Files/Git/api/status`. Prefix the command with `MSYS_NO_PATHCONV=1`
if you want to use Git Bash anyway.

## Where credentials live

`src/flash_block.cpp` owns the last 8 KB erase block of the SAMD51's 512 KB
flash, at a hardcoded `0x7E000`.

The address is pinned rather than left to the linker for a specific reason.
`FlashStorage_SAMD`'s macros place their storage inside `.text`, which puts it
within the range `bossac` rewrites on upload *and* lets the address drift
whenever the program's size changes. Either one destroys stored credentials on a
firmware update, silently. At `0x7E000` — some 460 KB above `__etext` — the block
is never written by an upload and never moves, so pairings survive `-t upload`.

`CredentialStore::begin()` logs a FATAL line if the image ever grows into that
block, or if the flash page geometry is not what the code assumes. Writes are
read-back verified.

## Measuring CPU and RAM

Build-time RAM reporting is incomplete: PlatformIO sums `.data + .bss` and omits
`.hsram`, the 1 KB of USB DMA buffers, so it undercounts by exactly 1024 bytes.
`GET /api/metrics` reports the real figure along with what static analysis cannot
know — heap use and peak stack depth.

    arm-none-eabi-size -A firmware.elf
    arm-none-eabi-nm --size-sort -S -r firmware.elf | awk '$3 ~ /^[bBdD]$/'

`metrics::begin()` paints the unused region between heap and stack with `0xC5`,
then scans upward for the first disturbed byte. That gives `stack_peak` and
`min_free_ever` — the narrowest the heap/stack gap has ever been, which is the
headroom number that actually matters.

CPU is measured two ways, since with no RTOS there is no idle task to compare
against. `busy_permille` is the share of wall time inside request handling, and
`loop_hz` is the polling rate, which falls under load. Signature verification is
timed with the Cortex-M4 DWT cycle counter.

## Roadmap

The signed API above is the control plane; the display side is next.

- [ ] Matrix rendering framework — swappable "apps" driving the RGB panel
- [ ] Weather app
- [ ] F1 race-flag app
- [ ] Clock app
- [ ] `/api/app` endpoint to switch/configure the active app remotely

## Build and test

    pio run -e adafruit_matrix_portal_m4 -t upload
    pio device monitor

    pio test -e adafruit_matrix_portal_m4    # runs on the board over serial
    pio test -e native                       # needs a host g++/clang++ on PATH

The tests in `test/test_apiauth/` cover SHA-256 and HMAC against FIPS 180-4 and
RFC 4231 vectors, plus signature and header-parsing behaviour. Everything under
`lib/apiauth/` is free of Arduino headers so it builds on the host too.

Copy `include/secrets.h.example` to `include/secrets.h` and fill in
`SECRET_SSID`/`SECRET_PASS`; the real file is gitignored.

## Versioning

Each build bakes `git describe --tags --always --dirty` into `FIRMWARE_VERSION`,
reported by `GET /` and printed on boot — so a flashed board can be checked
against what's in git. Tags follow `vMAJOR.MINOR.PATCH`; pushing a `v*` tag
triggers a GitHub Release with the board and release firmware `.bin` files
attached.
