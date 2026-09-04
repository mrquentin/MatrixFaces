# API Reference

| Method   | Path                        | Auth | Purpose                          |
| -------- | --------------------------- | ---- | -------------------------------- |
| `GET`    | `/`                         | no   | device info, pairing window state |
| `POST`   | `/pair`                     | window | issue credentials              |
| `GET`    | `/api/status`               | yes  | board, uptime, LED, RSSI, clock   |
| `POST`   | `/api/led`                  | yes  | `{"on": true}`                    |
| `GET`    | `/api/clients`               | yes  | list paired client ids            |
| `DELETE` | `/api/clients/<id>`          | yes  | revoke a client                   |
| `GET`    | `/api/apps`                  | yes  | list registered apps, their settings schemas, and which is active |
| `GET`    | `/api/app`                   | yes  | currently active app              |
| `POST`   | `/api/app`                   | yes  | `{"index": 0}`, switch the active app |
| `GET`    | `/api/apps/<index>/settings` | yes  | current setting values for that app |
| `POST`   | `/api/apps/<index>/settings` | yes  | partial update of that app's settings |
| `GET`    | `/api/metrics`               | yes  | CPU and RAM instrumentation       |
| `POST`   | `/api/ws-ticket`             | yes  | mint a short-lived ticket for the upgrade below |
| `GET`    | `/api/ws`                    | ticket | upgrade to the live-events WebSocket, on boards with `kHasWebSocket` |

See [Security model](security.md) for how `yes`/`window`-gated requests are
authenticated, [Client](client.md) for a ready-made client, and
[Metrics](metrics.md) for what `/api/metrics` reports.

## Two boards, one API

The same firmware runs on the Matrix Portal M4 and the MatrixPortal S3, and
answers identically apart from what the hardware makes possible.
`GET /api/status` says which one you reached (`"board":"matrixportal-m4"` or
`"matrixportal-s3"`), `/api/metrics` gains a `psram` section on the board that
has any, and the clock app offers a `tz` setting only where the board can act
on a POSIX timezone string — the M4 resolves its offset by geolocation instead.

A client should read the settings schema rather than assume it, which is what
the schema is for.

## App settings

Apps declare their own configuration; nothing about it is hardcoded in the
firmware's routing or a client's UI. `GET /api/apps` embeds each app's
settings schema so a generic controller can discover and render a form for
any app without prior knowledge of it:

    {"apps":[
      {"index":0,"name":"clock","settings":[
        {"key":"color","label":"Text color (0xRRGGBB)","type":"color","min":0,"max":16777215},
        {"key":"size","label":"Text scale (1-2)","type":"int","min":1,"max":2}
      ]},
      {"index":1,"name":"text","settings":[
        {"key":"text","label":"Display text","type":"string","max_len":31},
        {"key":"size","label":"Text scale (1-2)","type":"int","min":1,"max":2},
        {"key":"color","label":"Text color (0xRRGGBB)","type":"color","min":0,"max":16777215}
      ]}
    ],"active_index":0,"active_name":"clock"}

`type` is one of `bool`, `int`, `string`, `color`. `int` and `color` schemas
carry `min`/`max`; `string` schemas carry `max_len` (characters, excluding
the terminator); `bool` carries no extra constraints. `color` is wire-identical
to `int` (a plain JSON number, packed `0xRRGGBB`) -- it's a separate type
purely so a generic UI knows to render a color picker instead of a number
field, without hardcoding that "a setting named `color` is special".

`GET /api/apps/<index>/settings` returns just the current values, e.g.
`{"color":46335,"size":1}` (`46335` = `0x00B4FF`).

`POST /api/apps/<index>/settings` applies a partial update -- omitted keys
are left unchanged. A request with any unrecognized, malformed, or
out-of-range key is rejected in full (`400 invalid_setting_value`) before any
of that app's state is touched; the response on success is the same shape as
the `GET`, reflecting every value after the update.

Settings persist across a reboot: every successful `POST` snapshots every
registered app's every setting into flash (see [Where credentials and
settings live](flash-storage.md)), keyed by app name and setting key rather
than app index, and restores them at boot before the first app renders.
