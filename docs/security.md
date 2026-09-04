# Security Model

## Why not HTTPS

On the M4 it is not available: the network stack lives entirely on the ESP32
co-processor behind SPI, WiFiNINA exposes `WiFiSSLClient` (TLS *out*) but there
is no `WiFiSSLServer`, and the SAMD51 never sees a raw socket. The S3 could
terminate TLS — it owns its sockets — but it does not, deliberately: one
authentication model across both boards is worth more here than a certificate a
LAN device has no good way to obtain or rotate.

Instead, every API request is signed. The shared secret never crosses the wire
after pairing — only a signature over the request does — so a passive listener
on the LAN learns nothing reusable.

The one exposure left is the pairing response itself, which carries the secret
in the clear. It is gated by a physical button press, lasts 60 seconds, and is
consumed by the first successful pair. If that residual risk matters, the fix is
an ECDH handshake at pairing so the secret is never transmitted; put the board
behind a TLS-terminating reverse proxy if you need real HTTPS for clients.

## Outbound timezone lookup (M4 only)

`ClockApp` shows local time, but the M4 has no timezone database, so
`src/board/samd51/local_time.cpp` resolves the UTC offset from a plain HTTP GET
to `ip-api.com`, keyed off the board's public IP as seen by that service (not
sent explicitly — the service reads it from the connection). This means the
board's public IP and approximate location are disclosed to a third party,
roughly every 12 hours, over an unauthenticated plaintext connection. If that
is unacceptable, the fix is to stop calling it and let `ClockApp` render UTC —
it already does exactly that whenever the lookup hasn't resolved yet.

The S3 makes no such request. It gets UTC from SNTP and its offset from a POSIX
`TZ` string set through the clock app's `tz` setting, so the zone is stated
rather than inferred and nothing about the board leaves the LAN.

`TimeSource` itself is never affected on either board: it stays pure UTC, which
the signing scheme above depends on.

## Pairing

1. Press **UP** on the board. The onboard LED blinks for 60 seconds.
2. `POST /pair` within that window. The response contains `client_id` and
   `secret` — this is the only time the secret is ever shown.
3. The window closes on the first successful pair, or after 60 seconds.

Outside the window `/pair` returns `401 {"error":"pairing_closed"}`.

Credentials survive a reboot: a reserved flash block on the M4, an NVS
namespace on the S3. Up to 4 clients can be paired. **Hold DOWN for 5 seconds**
to revoke all of them.

They are stored in the clear on both boards. NVS can encrypt its contents, but
the key would live in the same flash an attacker holding the board already has,
and turning it on complicates recovery and OTA without changing the threat
model — which was, and remains, that physical possession of the board means
possession of its credentials. Revoking a lost board's clients is the answer,
not encryption at rest.

## Signing a request

    Authorization: HMAC id=<16 hex>,ts=<unix seconds>,nonce=<16 hex>,sig=<64 hex>

where `sig` is HMAC-SHA256 over the canonical request, keyed with the raw secret
bytes:

    METHOD \n TARGET \n TS \n NONCE \n SHA256HEX(body)

`TARGET` is the request target exactly as sent, query string included. A request
is rejected unless:

- the timestamp is within 60 seconds of the board's NTP-synced clock,
- the nonce has not been seen recently, and
- the timestamp is not older than the last request served for that client.

The signature is verified *before* any replay state is updated, so an
unauthenticated caller cannot advance a client's high-water mark to lock the
real client out.

Until the board gets NTP time from the WiFi module, API calls return
`503 {"error":"clock_unavailable"}` rather than accepting unbounded replays.

## The WebSocket upgrade's ticket

`GET /api/ws` cannot be signed the way every other route is: a browser's
`WebSocket` constructor never lets calling code set an `Authorization`
header, so there is nothing for the upgrade to carry a signature in.

Instead, a signed `POST /api/ws-ticket` mints a random 8-byte ticket, and
the upgrade is opened at `/api/ws?ticket=<16 hex>`. The ticket is:

- single-use -- consumed the moment the upgrade succeeds, so it cannot be
  replayed even if it leaks (a browser history entry, a proxy log);
- short-lived -- 10 seconds, long enough to receive the response and open
  the socket immediately after, nothing more;
- not itself a credential -- it proves only "something this board already
  authenticated a moment ago wants to open a socket now", the same
  guarantee a signature gives every other route, carried where a browser
  can actually put it.
