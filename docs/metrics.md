# Measuring CPU and RAM

`GET /api/metrics` reports what static analysis cannot know: heap use, how deep
the stack has actually gone, and where the CPU's time goes. The same JSON shape
comes back from both boards, but two of the RAM figures are measured very
differently, so they are worth reading with the board in mind.

## CPU

With no RTOS on the M4 there is no idle task to compare against, so CPU is
measured two ways: `busy_permille` is the share of wall time spent inside
request handling, and `loop_hz` is the polling rate, which falls under load.
Signature verification is timed separately (`auth_avg_us`, `auth_max_us`).

The arithmetic behind all of it — the one-second window, the request and auth
accumulators — is `src/board/metrics_counters.h`, shared by both boards. What
each board supplies is the clock: the Cortex-M4's DWT cycle counter on the
SAMD51, `ESP.getCycleCount()` on the S3. The S3's counter is per-core, which is
fine while everything runs on the loop task and is a constraint phase 4's task
split will have to respect.

## RAM, on the M4

Build-time reporting is incomplete: PlatformIO sums `.data + .bss` and omits
`.hsram`, the 1 KB of USB DMA buffers, so it undercounts by exactly 1024 bytes.

    arm-none-eabi-size -A firmware.elf
    arm-none-eabi-nm --size-sort -S -r firmware.elf | awk '$3 ~ /^[bBdD]$/'

`metrics::begin()` paints the unused region between heap and stack with `0xC5`,
then scans upward for the first disturbed byte. That gives `stack_peak` and
`min_free_ever` — the narrowest the heap/stack gap has ever been, which on a
board with one stack and no allocator to speak of is *the* headroom number.

## RAM, on the S3

Nothing is painted here, because the chip already keeps the books: FreeRTOS
records each task's deepest stack use and the allocator remembers the least
free it has ever had. So the same field names answer slightly narrower
questions:

| Field | M4 | S3 |
|---|---|---|
| `total` | all of SRAM | internal DRAM the allocator manages |
| `static` | everything below the heap | DRAM that never reached the heap: statics *plus* the chip's own reserves, which cannot be told apart at runtime |
| `stack_peak` | the deepest the single stack has been | the deepest the **loop task's** stack has been; other tasks are not counted |
| `min_free_ever` | narrowest heap/stack gap | least free internal heap |
| `psram` | absent | total and free external PSRAM |

The practical difference is `stack_peak`. On the M4 it covers the whole
program; on the S3 it covers one task out of several the WiFi driver already
runs, and phase 4.2 adds per-task high-water marks so the number stops being a
partial answer.

`psram` is omitted rather than zeroed on a board with no external RAM, so
"there is none" and "it is exhausted" cannot be confused.

## The MultiViewer poll

The `multiviewer` section exists because a feed that has quietly stopped
parsing looks exactly like a quiet session. `parsed` climbing while the display
sits on stale data says the shape of the feed changed under us; `malformed` or
`truncated` climbing says the same thing more loudly.

`connects` against `polls` is how the transport policy is read from outside:

| Board | Expected | Why |
|---|---|---|
| M4 | `connects == polls` | WiFiNINA shares a small socket pool between the server and every outbound connection, so the poll gives its socket back each time |
| S3 | `connects` ≈ 1 | lwIP owns its sockets, so the connection is held open across polls |

On the S3 a climbing `connects` means the server is dropping the connection
between polls, which is allowed and is handled — a reused socket that turns out
to be dead is retried once on a fresh one — but it means the keep-alive saving
is not being had.

The S3 also splits the query: `TrackStatus` and `LapCount` every two seconds,
`DriverList` and `RaceControlMessages` folded in every four — so every other
poll is the cheap one. Against `tools/mv_mock.py` that is a 137-byte response
instead of 633. The M4 asks for everything every time, as it always has.

The split is safe because the parser leaves state it was not told about alone —
`parseDriverList` and `parseRaceControlMessages` both return without touching
anything when their field is absent — and `mv_mock.py` returns only the fields
the query asked for, so a regression in that behaviour would show up in
testing rather than during a race.

Four seconds for the slow group is a deliberate middle. A blue flag arrives in
`RaceControlMessages`, so that interval is the longest one can be late; track
flags, shown in preference to it, stay on the two-second cadence. A longer slow
interval saves more traffic and, until phase 4.2 moves polling off the render
task, more of the stall that comes with it — but not enough to be worth a
staler flag.
