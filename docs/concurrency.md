# Concurrency

The ESP32-S3 runs four jobs at once. The rules below were written in phase 4.1,
before any of them could be broken, and phase 4.2 turned them on.

The M4 stays single-threaded permanently: it runs the same four jobs in turn,
one waiting for the last. It obeys the same rules because following them costs
it nothing, and because a rule only one board honours is a rule nobody checks.

| Job | S3 | M4 |
|---|---|---|
| `renderTick` | task, core 1, 12 KB | in turn |
| `netTick` | task, core 0, 12 KB | in turn |
| `wsTick` | task, core 0, 6 KB | in turn |
| `mvTick` | task, core 0, 8 KB | in turn |
| `housekeepTick` | Arduino loop task, core 1 | in turn |

Core 1 is the display's. Protomatter's row interrupt is allocated there by
`matrix.begin()`, and every `show()` has to come from the task that owns the
panel. Core 0 takes everything that can block, alongside lwIP's own task.

What that buys, measured with `mv_mock.py --delay 2` so a poll takes two
seconds: HTTP latency of **314 ms median on the S3 against 2063 ms on the M4**,
where every single request waits for the poll. Same firmware, same poll.

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
| App instances, their settings | render | the drain | the command queue |
| `SettingsBag` storage | render | the drain | `settingsMutex` — net reads it for `GET` |
| `AppScheduler::activeIndex_` | render | the drain | the command queue |
| `AppSettingsStore` | render | after a drain | only touched there |
| `desiredLedState` | render writes, housekeep reads | the drain | `std::atomic<bool>` |
| `PairingWindow` | housekeep writes, net reads/writes | button, `POST /pair` | one atomic word |
| `CredentialStore` | **net** | `POST /pair`, `DELETE`, the revoke flag | single task, by construction |
| `Authenticator` replay state | **net** | request handling | single task, by construction |
| `revokeAllRequested` | housekeep writes, net clears | DOWN button | `std::atomic<bool>`, `exchange` |
| `MultiViewerClient` + its buffer | mv | its own poll | single task, by construction |
| `MvLink` | both | mv publishes, render reads | mutex + one atomic |
| `TimeSource` | housekeep writes, render reads | `maintain()`, `setTz` | **see below** |
| `metrics` counters | any | `record*()` | plain 32-bit words |
| `net_link` sockets | net | accept/serve | one consumer per socket |

### Two things the split had to change

Writing this table is what found them; neither was a problem while one task ran
everything.

- **`CredentialStore` and `Authenticator`** were written by `POST /pair` on the
  network side *and* by the DOWN-button factory reset in housekeeping. Those
  are now different tasks on different cores. Rather than add a lock, the
  button raises `revokeAllRequested` and the network task does the work at the
  top of its next tick, so every write to both stays on one task.
- **`SettingsBag`** is read by `GET /api/apps/<n>/settings` on the network task
  while the drain writes it on the render task. A `bool` or an `int32_t` is
  written atomically; a `char[32]` is not, and a GET landing mid-write would
  see half of one value and half of another. `settingsMutex` covers the write
  and the read. It is deliberately **not** held across `onSettingChanged()`,
  which is app code that takes locks of its own — `F1FlagsApp`'s reaches into
  `MvLink` — because holding two at once creates an ordering someone has to
  remember.

### Still open

- **`TimeSource`** is refreshed by housekeeping and read by the clock app on
  the render task. `now()` is a single 32-bit read, but `localNow()` also
  consults zone state that `setTz()` writes. In practice `setTz` runs from the
  drain on the render task and the zone changes about never, so this is a
  narrow window rather than a live bug — but it is the last unguarded pair and
  should get a mutex or an atomic snapshot.
- **`netTick` still serves one HTTP connection at a time.** A hostile client
  can hold *HTTP* indefinitely — ~4s per connection, reconnecting immediately.
  What that no longer affects is anything else: rendering has its own core, and
  since the WebSocket moved to `wsTick` an open socket keeps receiving events
  throughout (measured: eight events at exact 2s intervals during a 25s block).
  Making HTTP itself concurrent needs a non-blocking request parser, which is a
  rewrite of `http_request` rather than a rearrangement, and has not been done.

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
