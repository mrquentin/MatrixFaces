# MatrixFaces → MatrixPortal S3 Migration — Context for the Planning Agent

> **Archived brief, kept verbatim.** This is the input document written on
> 2026-09-01 that seeded the S3 migration plan — it is a snapshot of intent and
> of the codebase *at that moment*, not living documentation. Where it disagrees
> with the tree, the tree wins. The plan it produced is what the migration
> actually follows; this file is here so the reasoning behind that plan stays
> in the repo.

## Instructions to you, the planning agent

You are planning (not yet implementing) the migration of the **MatrixFaces** firmware
from the Adafruit Matrix Portal M4 to the **Adafruit MatrixPortal S3**, including a
multi-board modular build so both targets can coexist in one repo.

**Your first action must be to ask the user for the filesystem path to the existing
MatrixFaces repository** (referred to below as "the old app"). Do not assume a
location. Once you have it, verify the layout against the "Current codebase" section
below and flag any divergence (this document was written 2026-09-01; the repo may
have moved on — in particular there was uncommitted F1-app work on branch
`feature/f1-flags-app` at the time of writing).

Your deliverable is a phased migration plan: concrete PR-sized steps, per-step
verification strategy, risk callouts, and an explicit file-level mapping of what is
created / moved / rewritten / **deleted** (this migration deletes more than it adds
in several places — the plan should say so precisely).

---

## 1. What MatrixFaces is today

A Matrix Portal M4 (SAMD51 @ 120MHz, 192KB RAM, 512KB flash, WiFi via ESP32
co-processor over SPI using **WiFiNINA**) driving a 128x64 HUB75 RGB LED matrix via
Adafruit Protomatter. ~4,500 lines of C++ (PlatformIO, Arduino framework).

Core concepts:

- **Swappable "apps"** (`App` interface + `AppScheduler`): clock, scrolling text,
  F1 race flags (polls MultiViewer's local GraphQL API at `<host>:10101`), and a
  flag test app. One app active at a time; `update(matrix, nowMs)` called every
  `loop()` iteration; apps self-throttle redraws.
- **Discoverable settings schema**: each app declares `SettingDescriptor`s
  (key/label/type/min/max/maxLen; types bool/int/string/color). `GET /api/apps`
  returns the schema so a generic client can render a form for any app with zero
  per-app knowledge. This design is a keeper — the migration should build on it.
- **Signed HTTP API** (plain HTTP, port 80): HMAC-SHA256 request signing
  (`Authorization: HMAC id=..,ts=..,nonce=..,sig=..` over
  `METHOD\nTARGET\nTS\nNONCE\nSHA256HEX(body)`), button-gated 60s pairing window,
  nonce cache + per-client timestamp high-water mark for replay defense, 503 until
  NTP sync. Documented in `docs/security.md`.
- **Persistence**: hand-rolled raw-flash storage (`flash_block` + templated
  `FlashRecordStore`) at two pinned erase blocks (credentials, app settings), with
  CRC, read-back verify, and an `__etext` overlap check — all built to survive
  firmware uploads on SAMD51 internal flash.
- **Metrics**: CPU/RAM instrumentation (stack painting, cycle counter), compiled
  out entirely with `-DMETRICS_ENABLED=0` (`[env:release]`).

### Current codebase layout

```
platformio.ini            envs: adafruit_matrix_portal_m4, release, native (host tests)
src/main.cpp              ~900 lines: composition root + router + ALL handlers + ad-hoc JSON
src/api/                  http_request (parse/respond), authenticator, credential_store, pairing_window
src/apps/                 app.h, app_scheduler, app_setting.h, app_settings_store,
                          clock_app, text_app, f1_flags_app, flag_test_app, flag_display
src/net/                  multiviewer_client (GraphQL poll + hand-rolled JSON parse),
                          timezone_offset (UTC offset via HTTP to ip-api.com)
src/board/                flash_block, flash_record_store, button, metrics, secure_random, time_source
lib/apiauth/              pure C++ HMAC/SHA256/hex/canonical-request — host-tested (test/test_apiauth)
tools/m4client.py         reference client (pairing + signed requests)
docs/                     security.md, api.md, client.md, flash-storage.md, metrics.md, development.md
.github/workflows/        ci.yml, release.yml (tag-triggered, bakes git-describe version)
include/secrets.h         compile-time WiFi credentials (gitignored; .example committed)
```

### Strengths to preserve

1. Security model rigor (verify-before-mutating-replay-state, constant-time compare,
   TRNG-gated pairing, honest documentation of residual risks).
2. Discoverable settings schema and the generic settings endpoints.
3. Allocation-free / fixed-buffer discipline (relax deliberately where the S3's RAM
   justifies it, not by accident).
4. `lib/apiauth` as a pure, host-tested library — must survive unchanged.
5. Comment culture: comments explain *why* and document invariants.
6. Metrics-compiled-out pattern.

### Known defects and debts (from an in-depth review; fix during migration)

- **`src/main.cpp` god file**: routing (if/strcmp chain), all handlers, a JSON
  parser (`extractJsonBool/UInt/Int/String`, `jsonHasKey`) and serializer
  (`appendJson`) all live there.
- **Three independent hand-rolled JSON parsers** (main.cpp, multiviewer_client.cpp,
  timezone_offset.cpp) with diverging semantics. Concrete bug: `jsonHasKey` matches
  keys inside string *values* — body `{"text":"say \"size\" now"}` on the text app's
  settings endpoint falsely detects a `size` key, fails to parse it, and rejects the
  whole request.
- **Settings validated three times** (descriptor, generic `valueSatisfiesDescriptor`,
  and each app's own `setSetting`), ~70 lines of near-identical boilerplate per app,
  and a `static constexpr kNone` sentinel descriptor duplicated in 5 files.
- **Blocking I/O in the single cooperative loop** (the structural flaw):
  MultiViewer poll can block ~10s (WiFiNINA connect) + 3s read; `connectWiFi()`
  blocks forever on WiFi loss; a slow-loris HTTP client can pin the loop for minutes
  (4s/line × 40 header lines); all of it freezes rendering, buttons, and pairing.
- **Hidden 32KB static buffer** inside `MultiViewerClient::poll()` (~17% of M4 RAM,
  claimed even when the app is unused); entire growing `RaceControlMessages` topic
  re-fetched every 2s.
- **`appendJson` truncates silently** → `/api/apps` (1KB buffer, ~850 bytes used at
  4 apps) will emit invalid JSON with `200 OK` as apps grow.
- **Every settings POST erases/rewrites a full 8KB flash block** (`saveAll`) —
  incompatible with real-time slider dragging.
- Test scaffolding (`FlagTestApp`) ships in release builds and is API-switchable.
- MultiViewer parser is pure string logic but untestable on host because its .cpp
  includes WiFiNINA for the unrelated transport code.
- Parse failures degrade silently with no counters — schema drift on MultiViewer's
  end is invisible.
- `handlePair` zeroes `secretHex` but not the `json` buffer that also holds the secret.
- Doc drift: README/roadmap/api.md predate the F1 app; `.idea/` churn in git.
- Compile-time WiFi credentials (`secrets.h`) — reflash to change SSID.
- Inconsistent dependency style: ClockApp gets deps injected; F1FlagsApp privately
  owns its network client.

---

## 2. Target hardware: Adafruit MatrixPortal S3

ESP32-S3, dual-core LX7 @ 240MHz, **512KB SRAM + 2MB PSRAM, 8MB flash**, native
WiFi/lwIP/mbedTLS (TLS both directions), BLE, native USB, FreeRTOS always on,
LIS3DH accelerometer, same UP/DOWN buttons and HUB75 form factor. Protomatter
supports the S3 (LCD_CAM + DMA) with the same API — apps and `flag_display` need
no changes beyond the pin table. (ESP32-HUB75-MatrixPanel-I2S-DMA is a viable
alternative library; default to Protomatter for a least-change port unless the plan
finds a hard reason.)

---

## 3. Required outcome 1: modular multi-board architecture

Both boards build from one repo. Hard rule: **`#ifdef`s live only in
`platformio.ini` and inside `src/board/`** — never in shared code.

- **Per-target implementation folders selected by `build_src_filter`**: shared
  headers in `src/board/*.h` define the contract; `src/board/samd51/`,
  `src/board/esp32s3/`, `src/board/native/` (host-test fakes) hold the `.cpp`s.
  Link-time selection, no vtables; a new board's missing pieces surface as link
  errors (a self-generating porting checklist).
- **Per-target headers via include path**: each env adds `-Isrc/board/<target>` so
  shared code can `#include "board_pins.h"` (Protomatter pins, panel geometry,
  button pins — currently hardcoded in main.cpp).
- **Per-env `lib_deps`** (WiFiNINA only for M4) and `extends`-based env inheritance
  (the repo already uses this for `[env:release]`).
- **Seams to cut (these make everything else cheap; several can land on the M4
  first as no-behavior-change PRs):**
  1. `Client&`/`Server&` (Arduino base classes) instead of `WiFiClient`/`WiFiServer`
     throughout `src/api/` and `src/net/` — both cores implement them.
  2. Blob-store interface (`load(name, buf, cap, &len)` / `save(...)`) replacing
     `FlashRecordStore`'s public face; SAMD implements it with the existing
     flash_block+CRC machinery, ESP32 with NVS `Preferences` (~20 lines).
     `CredentialStore`/`AppSettingsStore` keep their interfaces.
  3. `net_link.h`: WiFi bring-up/maintain (hides `WiFi.setPins`+NINA-reset vs
     `WiFi.begin`+events).
  4. Split `main.cpp` into `api/json_lite`, `api/router` (table-driven), and
     controllers; main.cpp becomes wiring + setup/loop only.
  5. Split `multiviewer_parse.{h,cpp}` (pure, host-testable) from transport.
- **Variant files for app registration**: `src/variants/{full,dev,clock_only}.cpp`
  each defining `registerApps(...)`, selected per env by src filter. Unregistered
  apps aren't compiled; `FlagTestApp` naturally drops out of release builds.
- **`env:native` grows**: with the seams + `board/native/` fakes, the JSON module,
  settings validation, and the MultiViewer parser (fed captured real responses) all
  run in host CI. Note: the user's machine has **no host C++ compiler** — native
  tests run in GitHub Actions CI only; on-hardware verification happens over serial
  (COM7).
- **CI matrix**: build every env on every PR so one board's change can't silently
  break the other.

## 4. Required outcome 2: S3 platform swap (delete-heavy)

| Module | Fate |
|---|---|
| `lib/apiauth`, apps, `app_scheduler`, `flag_display` | Unchanged |
| `api/*` | Near-unchanged after the `Client&` seam |
| `net/multiviewer_client` | Transport swap; parser identical; blocking-connect constraint (and its 30s backoff) disappears; poll `TrackStatus`/`LapCount` at ~2s but the heavy `RaceControlMessages` at ~10-15s; keep-alive connection instead of connect-per-poll |
| `net/timezone_offset` | **Delete.** `configTzTime()` + newlib TZ rules give DST-correct local time offline — also removes the documented ip-api.com privacy leak |
| `board/flash_block` + `flash_record_store` | **Delete (~300 lines).** NVS has wear leveling in its own partition; the `__etext`/geometry/CRC machinery solves problems NVS doesn't have |
| `board/time_source` | Thin SNTP wrapper |
| `board/secure_random` | `esp_fill_random()` |
| `board/metrics` | Same API; `uxTaskGetStackHighWaterMark` + `heap_caps` |
| `main.cpp` | New composition root per §3 |

**FreeRTOS task split** (fixes every blocking-I/O finding structurally):
- Render task (core 1): `AppScheduler::update()` at fixed cadence; never blocks.
- Network task(s) (core 0): HTTP server, MultiViewer poller, MQTT.
- All app-state mutation flows through a **command queue into the render task**
  (switch app, apply setting); MultiViewer state read via mutex-protected snapshot.
  This is the port's main *new* risk — the current code has never had data races —
  and the plan must treat cross-task ownership explicitly.

## 5. Required outcome 3: features (user explicitly wants all of these planned)

1. **MQTT + Home Assistant discovery** (chosen over an ESPHome rewrite — decision
   already made: keep the custom firmware, get ~90% of the integration). Publish HA
   discovery configs: active app → `select`, color settings → `light` (RGB),
   other settings → `number`/`text`/`switch` per the existing descriptor types,
   F1 flag + lap count → sensors. Entity set should be *generated from the settings
   schema*, not hardcoded per app. Availability topic + LWT.
2. **OTA updates**: ArduinoOTA or HTTP `Update`, dual-partition rollback (8MB flash),
   integrated with the existing tag-triggered GitHub release workflow.
3. **Real-time settings push** (the user's headline wish): authenticated `/ws`
   WebSocket — HMAC-sign the upgrade request exactly like existing `/api` calls,
   then the persistent connection is authenticated (per-connection message counter,
   no per-message nonces). Inbound `{"app","key","value"}` → command queue →
   applied within a frame; outbound broadcast of `setting_changed` /
   `app_switched` / `flag_changed` to all clients so multiple controllers stay in
   sync. **Decouple apply from persist**: apply instantly, debounce NVS writes
   (~5s quiet).
4. **mDNS**: advertise `matrixfaces.local` (+ service TXT with firmware version).
5. **SNTP + TZ database** local time (replaces TimezoneOffset; TZ string becomes a
   device setting).
6. **WiFi provisioning**: credentials in NVS; Improv (USB/BLE) or captive portal;
   `secrets.h` reduced to dev-only fallback or removed.
7. **On-device web UI**: small SPA served from LittleFS that renders settings forms
   directly from the `/api/apps` schema (zero per-app UI code) and talks to `/ws`.
8. **Pairing hardening**: keep the HMAC scheme (decision made: no mandatory HTTPS
   server; self-signed-cert UX isn't worth it on a LAN device). Close the one
   documented gap — the pairing secret traveling in cleartext — via ECDH
   (curve25519, mbedTLS) during pairing, or BLE proximity pairing. Also zero the
   pairing response buffer.
9. **Refactors bundled in** (from §1 defects): unified JSON module (or ArduinoJson
   — evaluate; the RAM constraint that justified hand-rolling is relaxed on S3),
   `SettingsBag` (descriptors bound to member storage + single `onSettingChanged`
   callback; kills triple validation and the 5× `kNone`), streaming/loud-failure
   JSON responses instead of silent truncation, parse-failure counters in
   `/api/metrics`, consistent dependency injection for apps.
10. **Nice-to-have backlog** (plan as optional later phases): animated GIF "faces"
    from LittleFS/PSRAM, accelerometer-driven app (LIS3DH), richer F1 screens
    (more MultiViewer topics), chained/larger panels.

## 6. Suggested phasing (the plan may refine, but respect the dependency order)

1. **Seams on the M4, behavior-identical** (verifiable by building the existing env
   + on-board smoke test): `Client&`; main.cpp split; JSON unification; blob-store
   interface; multiviewer parse/transport split + host tests in CI; SettingsBag.
2. **Multi-board scaffolding**: platformio.ini restructure, `board/samd51/` move,
   variants, CI matrix — still M4-only, still behavior-identical.
3. **S3 bring-up**: `board/esp32s3/` implementations, pins, matrix on screen, WiFi,
   NVS, SNTP/TZ, delete timezone_offset + flash machinery.
4. **Task split + command queue.**
5. **WebSocket + debounced persistence + web UI.**
6. **MQTT/HA discovery, OTA, mDNS, provisioning.**
7. **Pairing hardening + backlog.**

## 7. Environment and workflow facts

- User: MrQuentinet <quentinvilluis@gmail.com>, Windows 11, PlatformIO, Git Bash.
  Board flashing/monitoring over **COM7**. **No host C++ compiler locally** —
  `pio test -e native` only works in CI.
- GitHub repo `mrquentin/MatrixFaces`; PRs to `master`; CI + tag-triggered release
  workflows exist; firmware version baked via `extract_version.py` (git describe).
- At time of writing, branch `feature/f1-flags-app` had uncommitted F1 work
  (f1_flags_app, flag_display, flag_test_app, multiviewer_client) — check git
  status before planning file moves.
- Docs under `docs/` are good but drifting; the plan should include a docs update
  pass (security.md changes with ECDH/WS, api.md gains WS/MQTT, flash-storage.md
  becomes NVS, README roadmap).
