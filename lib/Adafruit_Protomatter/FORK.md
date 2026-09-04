# Adafruit_Protomatter, vendored

Upstream: https://github.com/adafruit/Adafruit_Protomatter — **v1.7.1**, which
was the latest release when this was taken (published 2025-09-05).

Vendored rather than pulled through `lib_deps` because it carries three local
changes. All three are in the **ESP32-S3 backend**; the SAMD51 path is
byte-for-byte upstream, and the one change that touches shared code
(`core.c`) is behind a macro only this backend defines.

## Status

As of bd matrix-faces-sjz, the MatrixPortal S3 no longer uses this driver:
it moved to ESP32-HUB75-MatrixPanel-DMA's continuous DMA, which paces
OE/latch/row-select from hardware descriptor timing instead of a per-row
timer ISR, and so has no per-row ISR left for the mechanism below to delay.
The three changes and the "Known remaining issue" stay documented here as
the record of what was tried and measured, and because the M4 still runs
this exact vendored copy permanently -- but nothing on the S3 exercises the
ESP32-S3 backend below anymore. There is no equivalent of `env:s3_diag` for
the new driver: it has no ISR to instrument the same way.

## Why

The MatrixPortal S3 showed rows flashing brighter than the frame contained,
constantly while WiFi was associated. The same panel on the Matrix Portal M4,
same firmware, did not.

The mechanism is in `_PM_row_handler()`:

```c
IRAM_ATTR
void _PM_row_handler(Protomatter_core *core) {
  _PM_setReg(core->oe); // Disable LED output   <-- first instruction
```

LED output stays on from the `clearReg(oe)` at the end of one invocation until
the *next* one runs. The timer fires on schedule, so if entering the ISR is
delayed, the previous row simply stays lit for that much longer. The error is
unbounded and worst on the shortest bitplane: plane 0 lasts ~488 timer ticks,
so a 5000-tick delay lights that row about eleven times too long.

The M4 is immune because it bit-bangs GPIO from a short, deterministic ISR
with no instruction cache and no DMA driver in the path.

## Measurements

Taken with `env:s3_diag` (`src/diag/flicker_probe.cpp`), which counts the
per-row `elapsed` reading that feeds `bitZeroPeriod`. Floor (`minPeriod`) is
488 ticks in every run.

| Configuration | worst `elapsed` | visible |
|---|---|---|
| No WiFi | 1389 | no |
| No WiFi, level-3 interrupt | 1358 | no |
| **WiFi, upstream** | **5834** | yes, constant |
| WiFi, level-3 interrupt | 2142 | much reduced |
| WiFi, level 3, radio pinned to core 0 | 2155 | unchanged |
| WiFi, level 3, core 1 given to the display alone | 2162 | unchanged |
| **WiFi, level 3 + GDMA inlined** | **1981** | rare |

Two negative results worth keeping: neither moving the radio to the other core
nor dedicating a whole core to the display changed the number at all. That is
what identified the residual as the *shared instruction cache* rather than core
contention — a cache stall is not a preemption, and the cache serves both cores.

## The changes

1. **`arch/esp32-common.h` — interrupt priority.** Upstream calls
   `timerAttachInterrupt()`, which passes `intr_alloc_flags = 0`: the lowest
   level, shared with WiFi. Changed to `timerAttachInterruptFlag(...,
   ESP_INTR_FLAG_LEVEL3)`. Level 3 is the highest a C handler may use.
   `ESP_INTR_FLAG_IRAM` is deliberately *not* set — it asserts the whole path
   is safe with the flash cache disabled, which is untrue, and setting it
   reboot-loops the board.

2. **`arch/esp32-s3.h` — `gdma_start()` inlined.** That function is called
   from the ISR on every scanline and lives in flash: the Arduino core's
   prebuilt IDF has `CONFIG_GDMA_CTRL_FUNC_IN_IRAM` unset. It also takes a
   spinlock. Replaced with the two register writes it performs, using a channel
   id captured at init via `gdma_get_channel_id()`, and falling back to the
   driver call if that id was never obtained.

3. **`arch/esp32-s3.h` + `core.c` — a ceiling on the adaptive period.**
   `bitZeroPeriod` had a floor (`minPeriod`) but no upper bound, so a single
   inflated reading stretched bitplane N by `period << N`. `core.c` now applies
   `_PM_periodCeiling(core)` where a backend defines it; only the S3 backend
   does. **This must not be defined for the SAMD51**, where `elapsed` is a real
   measurement of how long issuing a row took — capping it there could start the
   next row before the current one finished.

## Known remaining issue

`_PM_timerStart()`/`_PM_timerStop()` still call `timerAlarmWrite()`,
`timerAlarmEnable()`, `timerStart()` and `timerStop()`, none of which carry
`IRAM_ATTR` in `esp32-hal-timer.c`. So the ISR still makes four flash-resident
calls per row, which is the most likely source of the residual rare spikes.
Fixing it means reimplementing the timer-group register writes inline, which is
a larger and more version-sensitive change than the three above. Tracked in
beads; not attempted.

## Upgrading

These are three small, self-contained edits, each marked `LOCAL CHANGE` in the
source. On a new upstream release, diff the vendored tree against it, re-apply,
and re-run `env:s3_diag` — the numbers above are the regression test. If
upstream ever adopts equivalents, drop the fork and go back to `lib_deps`.
