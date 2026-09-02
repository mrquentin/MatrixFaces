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
