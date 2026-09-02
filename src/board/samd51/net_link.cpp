#include "board/net_link.h"

#include <Arduino.h>
#include <SPI.h>
#include <WiFiNINA.h>

#include "secrets.h"

namespace net_link {
namespace {

// Constructed at file scope like any other Arduino sketch global; the port is
// fixed at construction, so serveHttp() only starts it listening.
WiFiServer g_server(80);
bool g_serving = false;

// The connection accept() last handed out. One at a time: loop() serves a
// request to completion before taking the next.
WiFiClient g_request;

// The MultiViewer poll's socket.
WiFiClient g_outbound;

// Blinks the LED forever after printing a fatal boot error; never returns.
// Duplicated from main.cpp deliberately: a radio that is absent is a
// board-level fault and this file should not need to reach upward to report it.
[[noreturn]] void haltBlinking(const __FlashStringHelper *message) {
  Serial.println(message);
  while (true) {
    digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) == LOW ? HIGH : LOW);
    delay(100);
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
  do {
    Serial.print(F("Connecting to "));
    Serial.println(SECRET_SSID);
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    delay(2000);
  } while (WiFi.status() != WL_CONNECTED);

  Serial.println(F("Connected!"));
  printStatus();
}

}  // namespace

void begin() {
  // The Matrix Portal M4's ESP32 co-processor is on non-default pins.
  WiFi.setPins(SPIWIFI_SS, SPIWIFI_ACK, ESP32_RESETN, ESP32_GPIO0, &SPIWIFI);

  if (WiFi.status() == WL_NO_MODULE) {
    haltBlinking(F("Communication with WiFi module failed!"));
  }

  Serial.print(F("WiFi module firmware: "));
  Serial.println(WiFiClass::firmwareVersion());

  connect();
}

bool maintain() {
  if (WiFi.status() == WL_CONNECTED) return false;

  Serial.println(F("WiFi lost, reconnecting"));
  connect();
  // The listening socket does not survive a reconnect.
  if (g_serving) g_server.begin();
  return true;
}

bool isUp() { return WiFi.status() == WL_CONNECTED; }

IPAddress ip() { return WiFi.localIP(); }

int32_t rssiDbm() { return WiFi.RSSI(); }

const char *ssid() { return WiFi.SSID(); }

void serveHttp(uint16_t port) {
  (void)port;  // fixed at construction; see g_server
  g_server.begin();
  g_serving = true;
}

Client *accept() {
  g_request = g_server.available();
  return g_request ? &g_request : nullptr;
}

void finishRequest() {
  // The delay lets the NINA flush the response before the socket goes away;
  // without it a client can see a reset instead of the last bytes.
  delay(10);
  g_request.stop();
}

Client &outboundClient() { return g_outbound; }

}  // namespace net_link
