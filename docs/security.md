# Security Model

## Why not HTTPS

The board's network stack lives entirely on the ESP32 co-processor behind SPI.
WiFiNINA exposes `WiFiSSLClient` (TLS *out*) but there is no `WiFiSSLServer`,
and the SAMD51 never sees a raw socket, so it cannot terminate TLS itself.

Instead, every API request is signed. The shared secret never crosses the wire
after pairing — only a signature over the request does — so a passive listener
on the LAN learns nothing reusable.

The one exposure left is the pairing response itself, which carries the secret
in the clear. It is gated by a physical button press, lasts 60 seconds, and is
consumed by the first successful pair. If that residual risk matters, the fix is
an ECDH handshake at pairing so the secret is never transmitted; put the board
behind a TLS-terminating reverse proxy if you need real HTTPS for clients.

## Outbound timezone lookup

`ClockApp` shows local time, but the board has no timezone database, so
`TimezoneOffset` resolves the UTC offset from a plain HTTP GET to
`ip-api.com`, keyed off the board's public IP as seen by that service (not
sent explicitly — the service reads it from the connection). This means the
board's public IP and approximate location are disclosed to a third party,
roughly every 12 hours, over an unauthenticated plaintext connection. If that
is unacceptable, the fix is to drop `TimezoneOffset` and let `ClockApp` render
UTC — it already does exactly that whenever the lookup hasn't resolved yet.
`TimeSource` itself is never affected: it stays pure UTC, which the signing
scheme above depends on.

## Pairing

1. Press **UP** on the board. The onboard LED blinks for 60 seconds.
2. `POST /pair` within that window. The response contains `client_id` and
   `secret` — this is the only time the secret is ever shown.
3. The window closes on the first successful pair, or after 60 seconds.

Outside the window `/pair` returns `401 {"error":"pairing_closed"}`.

Credentials are written to the SAMD51's emulated EEPROM, so they survive a
reboot. Up to 4 clients can be paired. **Hold DOWN for 5 seconds** to revoke all
of them.

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
