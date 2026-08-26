# API Reference

| Method   | Path                 | Auth | Purpose                          |
| -------- | -------------------- | ---- | -------------------------------- |
| `GET`    | `/`                  | no   | device info, pairing window state |
| `POST`   | `/pair`              | window | issue credentials              |
| `GET`    | `/api/status`        | yes  | uptime, LED, RSSI, clock          |
| `POST`   | `/api/led`           | yes  | `{"on": true}`                    |
| `GET`    | `/api/clients`       | yes  | list paired client ids            |
| `DELETE` | `/api/clients/<id>`  | yes  | revoke a client                   |
| `GET`    | `/api/metrics`       | yes  | CPU and RAM instrumentation       |

See [Security model](security.md) for how `yes`/`window`-gated requests are
authenticated, [Client](client.md) for a ready-made client, and
[Metrics](metrics.md) for what `/api/metrics` reports.
