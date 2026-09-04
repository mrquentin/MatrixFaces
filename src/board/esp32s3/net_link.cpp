#include "board/net_link.h"

#include <Arduino.h>
#include <WiFi.h>
#include <fcntl.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <cstring>

#include "secrets.h"

// The S3 has its radio on-die, so this file is shorter than the M4's in every
// way that matters: no SPI co-processor to find, no pin table, and a driver
// that reconnects on its own. What it owes the contract is the same -- come up,
// stay up, hand out sockets -- and one thing more: say *why* a link dropped,
// which the polled status cannot.
//
// Credentials still come from include/secrets.h here. Phase 6.1 replaces that
// with NVS plus a captive portal, at which point secrets.h goes back to being
// the M4's alone.
namespace net_link {
namespace {

constexpr uint32_t kConnectPollMs = 250;
constexpr uint32_t kStalledReportMs = 5000;

// Own the listening socket with raw BSD calls instead of WiFiServer, whose
// available()/accept() (framework-arduinoespressif32's
// libraries/WiFi/src/WiFiServer.cpp) leaks the accepted file descriptor
// whenever either setsockopt() call after lwip_accept() fails. That happens
// whenever the peer has already reset the connection in the gap between
// accept() and setsockopt() -- exactly what a burst of short-timeout
// concurrent clients produces against a server that drains one connection
// at a time. CONFIG_LWIP_MAX_SOCKETS is 16 on this board; a few dozen such
// leaks under load exhausts the whole socket pool and wedges every socket,
// not just this one, until reboot. Measured: bd matrix-faces-e5c.
//
// WiFiServer offers no way to fix just the accept path -- its listen fd is
// a private member -- so the listen socket is owned here directly instead,
// duplicating the handful of raw-socket calls WiFiServer::begin()/
// available() make, with the one difference that matters: the accepted
// socket is closed on any setsockopt() failure rather than dropped.
constexpr uint16_t kHttpPort = 80;
constexpr int kListenBacklog = 8;

int g_listenFd = -1;
bool g_serving = false;

void closeListenSocket() {
  if (g_listenFd < 0) return;
  lwip_close(g_listenFd);
  g_listenFd = -1;
}

void openListenSocket() {
  closeListenSocket();

  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;

  int enable = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(kHttpPort);
  if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0 ||
      listen(fd, kListenBacklog) < 0) {
    lwip_close(fd);
    return;
  }
  fcntl(fd, F_SETFL, O_NONBLOCK);
  g_listenFd = fd;
}

// The fixed equivalent of WiFiServer::available(): closes the accepted
// socket on any setsockopt() failure instead of leaking the fd.
//
// Also recycles the listen socket itself on a timer, independent of
// whether it looks unhealthy. Measured (bd matrix-faces-e5c): under a burst
// of concurrent short-lived connections against this single-connection-at-
// a-time server, the listen socket can stop producing acceptable
// connections at all -- lwip_accept() returns EAGAIN indefinitely even
// though the general socket pool still has headroom (checked with a
// throwaway socket() call, which kept succeeding) and the WiFi link and
// every FreeRTOS task stay healthy. The exact lwIP-internal mechanism was
// not pinned down further, but recycling the listener -- the same
// close()+reopen restartServer() already does after a WiFi reconnect --
// reliably clears it well inside the interval below, where previously only
// a reboot did.
constexpr uint32_t kRecycleIntervalMs = 30000;

WiFiClient acceptClient() {
  if (g_listenFd < 0) return WiFiClient();

  static uint32_t lastRecycleMs = 0;
  const uint32_t now = millis();
  if (now - lastRecycleMs > kRecycleIntervalMs) {
    lastRecycleMs = now;
    openListenSocket();
  }

  struct sockaddr_in clientAddr {};
  socklen_t clientLen = sizeof(clientAddr);
  const int clientSock =
      lwip_accept(g_listenFd, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);
  if (clientSock < 0) return WiFiClient();

  // TCP_NODELAY unconditionally: small responses otherwise wait on Nagle for
  // an ACK that is not coming -- the M4's NINA firmware does not batch
  // writes this way, but every S3 response benefits the same way regardless
  // of who called serveHttp().
  int enable = 1;
  if (setsockopt(clientSock, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) == 0 &&
      setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) == 0) {
    return WiFiClient(clientSock);
  }
  lwip_close(clientSock);
  return WiFiClient();
}

// The connection accept() last handed out. One at a time: loop() serves a
// request to completion before taking the next.
WiFiClient g_request;

// The MultiViewer poll's socket. The S3 does not have the NINA's tiny shared
// socket pool, but keeping the outbound connection distinct from the server's
// is still how the contract reads, and phase 4 will want it owned by one task.
WiFiClient g_outbound;

// Written by the WiFi driver's event task, read by the loop. A single byte,
// only ever used for a log line, so a torn read would cost nothing -- and it
// is the whole of this build's cross-task state. Phase 4.1's rtos seam is
// where that stops being true and this gets a proper home.
volatile uint8_t g_lastDisconnectReason = 0;

// The link state maintain() last saw, so it can report the up edge and only
// that. begin() sets it once it has connected: without that the first
// maintain() would see false -> true and tell the caller the link had just
// come back, when it had merely started.
bool g_wasUp = false;

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    g_lastDisconnectReason = static_cast<uint8_t>(info.wifi_sta_disconnected.reason);
  }
}

void printStatus() {
  Serial.print(F("SSID: "));
  Serial.println(WiFi.SSID());
  // noinspection HttpUrlsUsage  -- the board serves plain HTTP by design
  Serial.print(F("IP address: http://"));
  Serial.println(WiFi.localIP());
  Serial.print(F("Signal strength (RSSI): "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
}

void connect() {
  Serial.print(F("Connecting to "));
  Serial.println(SECRET_SSID);

  WiFi.begin(SECRET_SSID, SECRET_PASS);

  uint32_t waited = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(kConnectPollMs);
    waited += kConnectPollMs;

    // The driver retries association by itself, so this can wait a long time
    // without anything appearing on the wire. Say why periodically, because
    // the difference between "the AP is out of range" and "the password is
    // wrong" -- the second of which the driver deliberately does not retry --
    // is the disconnect reason and nothing else.
    if (waited % kStalledReportMs == 0) {
      Serial.print(F("[wifi] still connecting, last disconnect reason "));
      Serial.println(g_lastDisconnectReason);
    }
  }

  Serial.println(F("Connected!"));
  printStatus();
}

// The listening socket is bound to every interface and survives an IP change,
// but rebinding it after a reconnect costs nothing and keeps the two boards'
// behaviour identical rather than subtly different.
void restartServer() {
  if (!g_serving) return;
  openListenSocket();
}

}  // namespace

void begin() {
  WiFi.onEvent(onWiFiEvent);

  WiFi.mode(WIFI_STA);
  // The driver retries on its own, so maintain() only has to notice and report.
  WiFi.setAutoReconnect(true);
  // Modem sleep would add up to a beacon interval of latency to every request
  // for power this board does not need to save: it is mains-powered and
  // driving a 128x64 panel, next to which the radio is a rounding error.
  WiFi.setSleep(false);

  connect();
  g_wasUp = true;
}

bool maintain() {
  const bool up = isUp();
  if (up == g_wasUp) return false;
  g_wasUp = up;

  if (!up) {
    Serial.print(F("WiFi lost (reason "));
    Serial.print(g_lastDisconnectReason);
    Serial.println(F("), driver is retrying"));
    return false;
  }

  Serial.println(F("WiFi back"));
  printStatus();
  restartServer();
  return true;
}

bool isUp() { return WiFi.status() == WL_CONNECTED; }

IPAddress ip() { return WiFi.localIP(); }

int32_t rssiDbm() { return WiFi.RSSI(); }

const char *ssid() {
  // WiFi.SSID() returns a String by value here, unlike the NINA's char*, so
  // the bytes have to be kept somewhere that outlives the expression.
  static char name[33];  // 32-byte SSID plus terminator, per 802.11
  const String current = WiFi.SSID();
  strncpy(name, current.c_str(), sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
  return name;
}

void serveHttp(uint16_t port) {
  (void)port;  // fixed; see kHttpPort
  openListenSocket();
  g_serving = true;
}

Client *accept() {
  g_request = acceptClient();
  return g_request ? &g_request : nullptr;
}

void finishRequest() {
  // No settling delay here: the M4 needs one so the NINA co-processor can push
  // the response out before the socket disappears, whereas lwIP owns the bytes
  // as soon as they are written and closes gracefully behind them.
  g_request.flush();
  g_request.stop();
}

Client &outboundClient() { return g_outbound; }

// --- retained connections ---------------------------------------------------
//
// A WiFiClient copy shares the underlying socket by reference count here, so
// assigning into a slot keeps the connection alive after g_request is reused
// by the next accept(). That is exactly what makes this possible on the S3 and
// not on the M4, where the NINA hands out a small fixed pool.
namespace {

WiFiClient g_retained[kMaxRetained];

}  // namespace

int8_t retain() {
  for (uint8_t i = 0; i < kMaxRetained; ++i) {
    if (g_retained[i]) continue;
    g_retained[i] = g_request;
    // Stop lending it: the slot owns it now, and finishRequest() must not
    // close it out from under the slot.
    g_request = WiFiClient();
    return static_cast<int8_t>(i);
  }
  return -1;
}

Client *retained(uint8_t slot) {
  if (slot >= kMaxRetained) return nullptr;
  return g_retained[slot] ? &g_retained[slot] : nullptr;
}

void release(uint8_t slot) {
  if (slot >= kMaxRetained) return;
  if (g_retained[slot]) g_retained[slot].stop();
  g_retained[slot] = WiFiClient();
}

}  // namespace net_link
