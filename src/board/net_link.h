#pragma once

#include <Client.h>
#include <IPAddress.h>

#include <cstdint>

// The network link, as much of it as shared code is allowed to know: bring it
// up, keep it up, ask whether it is up. Which radio, which pins, and how
// credentials are supplied are all the board's business.
//
// This is what finally gets WiFiNINA out of main.cpp; after this it appears
// only under src/board/samd51/.
namespace net_link {

// Brings the radio up and connects. On the M4 this blocks until it succeeds,
// which is the behaviour the firmware has always had -- the render loop does
// not start until there is a link. Halts the board if the radio is missing.
void begin();

// Call from loop(). Reconnects if the link dropped.
//
// Returns true only on the call where the link came *back*, so the caller can
// restart anything bound to it -- the HTTP server's listening socket does not
// survive a reconnect.
bool maintain();

bool isUp();

IPAddress ip();

// Signal strength in dBm. Meaningless while the link is down.
int32_t rssiDbm();

// The network the board is on, for logging and /api/status.
const char *ssid();

// --- the listening HTTP socket ---------------------------------------------
//
// Arduino's Server base class is not usable as a seam -- it has no accept
// operation -- so the concrete server type stays board-side and callers only
// ever see a Client. maintain() rebinds it after a reconnect, since the
// listening socket does not survive one.

void serveHttp(uint16_t port);

// The next waiting connection, or nullptr if none. The returned client stays
// valid until the following accept(); call finishRequest() when done with it.
Client *accept();

// Flushes and closes whatever accept() last returned.
void finishRequest();

// --- outbound ---------------------------------------------------------------

// A dedicated socket for the one long-lived outbound consumer (the MultiViewer
// poll). Separate from anything the server is doing, because WiFiNINA hands out
// a small shared pool and two consumers on one instance would tear down each
// other's connections. If a second consumer ever appears it gets its own
// accessor rather than an index nobody can read.
Client &outboundClient();

}  // namespace net_link
