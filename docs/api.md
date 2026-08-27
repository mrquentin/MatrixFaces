# API Reference

| Method   | Path                        | Auth | Purpose                          |
| -------- | --------------------------- | ---- | -------------------------------- |
| `GET`    | `/`                         | no   | device info, pairing window state |
| `POST`   | `/pair`                     | window | issue credentials              |
| `GET`    | `/api/status`               | yes  | uptime, LED, RSSI, clock          |
| `POST`   | `/api/led`                  | yes  | `{"on": true}`                    |
| `GET`    | `/api/clients`               | yes  | list paired client ids            |
| `DELETE` | `/api/clients/<id>`          | yes  | revoke a client                   |
| `GET`    | `/api/apps`                  | yes  | list registered apps, their settings schemas, and which is active |
| `GET`    | `/api/app`                   | yes  | currently active app              |
| `POST`   | `/api/app`                   | yes  | `{"index": 0}`, switch the active app |
| `GET`    | `/api/apps/<index>/settings` | yes  | current setting values for that app |
| `POST`   | `/api/apps/<index>/settings` | yes  | partial update of that app's settings |
| `GET`    | `/api/metrics`               | yes  | CPU and RAM instrumentation       |

See [Security model](security.md) for how `yes`/`window`-gated requests are
authenticated, [Client](client.md) for a ready-made client, and
[Metrics](metrics.md) for what `/api/metrics` reports.

## App settings

Apps declare their own configuration; nothing about it is hardcoded in the
firmware's routing or a client's UI. `GET /api/apps` embeds each app's
settings schema so a generic controller can discover and render a form for
any app without prior knowledge of it:

    {"apps":[
      {"index":0,"name":"clock","settings":[]},
      {"index":1,"name":"text","settings":[
        {"key":"text","label":"Display text","type":"string","max_len":31},
        {"key":"size","label":"Text scale (1-2)","type":"int","min":1,"max":2}
      ]}
    ],"active_index":0,"active_name":"clock"}

`type` is one of `bool`, `int`, `string`. `int` schemas carry `min`/`max`;
`string` schemas carry `max_len` (characters, excluding the terminator);
`bool` carries no extra constraints.

`GET /api/apps/<index>/settings` returns just the current values, e.g.
`{"text":"Hello!","size":1}`.

`POST /api/apps/<index>/settings` applies a partial update -- omitted keys
are left unchanged. A request with any unrecognized, malformed, or
out-of-range key is rejected in full (`400 invalid_setting_value`) before any
of that app's state is touched; the response on success is the same shape as
the `GET`, reflecting every value after the update.
