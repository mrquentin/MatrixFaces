# Measuring CPU and RAM

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
