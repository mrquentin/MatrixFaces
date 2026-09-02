# Concurrency

Written before there is any concurrency, which is the point. Phase 4.2 splits
the ESP32-S3 across two cores; this is the contract that split has to obey, and
it is enforceable now, while there is one task and every rule is trivially
true.

The M4 stays single-threaded permanently. It obeys the same rules because
following them costs it nothing and because a rule only one board honours is a
rule nobody checks.

## The one rule

**A `Command` is the only way app state is mutated.**

Whoever handles a request validates it on whatever task it arrived on, then
posts a command. The task that owns the display drains the queue and applies
it. Nothing else writes to an app, a setting, or the LED.

```
    HTTP handler                     render loop
    ------------                     -----------
    validate against the bag
    rtos::commandPost(...)  ------>  rtos::commandTake(...)
    reply with what it accepted      applySetting() / switchTo()
```

Validation happens on the caller so a bad request is rejected with a 400 by the
task that can still answer it. Application happens on the owner so no app is
ever written from two places.

A full queue is a real failure, not something to paper over: `commandPost`
returns false and the API answers `503 busy`. A settings request checks
`commandFree()` before posting any of its commands, so it either lands whole or
is refused whole.

### What this costs

The reply to a mutating request is sent before the mutation has been applied —
they differ by up to one drain. So the reply reports **what was accepted**, not
what is live: `sendAppSettings()` prefers the values just queued, and
`POST /api/app` reports the index it was given. Reading the live value there
would tell a client its request had not taken effect.

Persistence moved for the same reason. `saveAll()` in the handler would write
the values the queued commands are about to replace, so `main.cpp` saves after
draining instead. Phase 5.2 makes that debounced rather than immediate.

## Ownership

Every piece of mutable state, and what keeps it safe. This table is the thing
to update — and to be asked about in review — whenever state is added.

| State | Owner | Written by | Guard |
|---|---|---|---|
| App instances, their settings | render | drain only | the command queue |
| `AppScheduler::activeIndex_` | render | drain only | the command queue |
| `desiredLedState` | render | drain only | the command queue |
| `AppSettingsStore` | render | after a drain | only touched there |
| `PairingWindow` | shared | button, `POST /pair` | one atomic word |
| `CredentialStore` | shared | `POST /pair`, DOWN button | **nothing yet — see below** |
| `Authenticator` replay state | net | request handling | single reader/writer today |
| `MultiViewerClient` + its buffer | render | its own poll | owned by the app that polls |
| `TimeSource` | render | `maintain()`, `setTz` | single writer today |
| `metrics` counters | any | `record*()` | plain 32-bit words |
| `net_link` sockets | net | accept/serve | one consumer per socket |

### Known gaps, to be closed by 4.2

These are safe today because there is one task. They are written down so the
split cannot quietly break them.

- **`CredentialStore`** is written by `POST /pair` and by the DOWN-button
  factory reset. Once those are on different tasks it needs a guard — most
  likely a command, since the mutations are rare and coarse.
- **`Authenticator`** holds per-client replay state read and written during
  request handling. Fine while one task serves requests; 5.1's WebSocket
  connections are the first thing that could change that.
- **`TimeSource`** is refreshed from the loop and read by the clock app. If
  those separate, `localNow()` needs a coherent read of the offset.
- **`MvSnapshot`** does not exist yet. Phase 4.2 introduces it precisely
  because the MultiViewer poll and the app that reads its results will no
  longer share a task.

## The seam

`board/rtos.h` is the contract; each board implements it.

| | Queue | Mutex |
|---|---|---|
| `samd51` | ring buffer (`board/command_ring.h`) | no-op |
| `native` | the same ring buffer | no-op |
| `esp32s3` | FreeRTOS queue | FreeRTOS mutex |

The ring buffer has no locking and must not be posted to from an interrupt: on
those boards there is exactly one task, and a no-op mutex around it would only
invite someone to believe otherwise. The S3's queue is a FreeRTOS one because
from 4.2 the poster and the drainer are on different cores, which needs real
memory ordering rather than a lucky one.

`Mutex` is a no-op on the single-task boards so shared code can take a lock
unconditionally and the boards that do not need one pay nothing. Nothing takes
it yet; 4.2 does, for the MultiViewer snapshot and for `SettingsBag`'s string
values.

There is no event queue yet. The plan puts one here, but it has no producer and
no consumer until phase 5.2 fans changes out to WebSocket and MQTT clients, and
its shape — depth, and what happens when it fills — is a decision that belongs
with the code that uses it. The ring buffer it will need already exists.

## Constraints the S3 imposes on 4.2

Measured, not assumed, while chasing the display artefact in
`lib/Adafruit_Protomatter/FORK.md`:

- **A task on core 0 must yield.** `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0`
  is set in the Arduino core's sdkconfig and CPU1's equivalent is not, so a
  loop that never blocks on core 0 panics the board within seconds. Moving the
  network task there means giving the idle task air.
- **Dedicating a core to rendering does not improve display timing.** It was
  tried and measured: no change. Split the tasks for the reasons in the plan —
  a poll or a slow client must not stall the panel — but do not expect the
  artefact to move, and re-measure with `env:s3_diag` rather than guessing.
- **Protomatter's row interrupt runs at level 3.** Anything else that wants a
  high-priority interrupt on the S3 has to coexist with it.
