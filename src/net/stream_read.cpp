#include "stream_read.h"

#include <Arduino.h>

namespace net {
namespace {

// Called after read() came up empty. False means "stop waiting": the peer has
// closed and left nothing buffered, so no further byte is ever coming.
// Otherwise it yields for a tick and the caller retries.
bool waitForMore(Client &client) {
  if (!client.connected() && client.available() == 0) return false;
  delay(1);
  return true;
}

}  // namespace

LineStatus readLine(Client &client, char *out, size_t cap, size_t &outLen, uint32_t timeoutMs) {
  size_t n = 0;
  bool overflowed = false;
  const uint32_t start = millis();

  // The deadline is checked per byte, not just while starved, so the cap holds
  // even against a peer that is technically still making progress.
  while (millis() - start < timeoutMs) {
    const int c = client.read();
    if (c < 0) {
      if (!waitForMore(client)) break;
      continue;
    }

    if (c == '\n') {
      while (n > 0 && out[n - 1] == '\r') --n;
      out[n] = '\0';
      outLen = n;
      return overflowed ? LineStatus::kTruncated : LineStatus::kOk;
    }

    if (n + 1 < cap) {
      out[n++] = static_cast<char>(c);
    } else {
      overflowed = true;
    }
  }

  out[0] = '\0';
  outLen = 0;
  return LineStatus::kTimeout;
}

bool readExactly(Client &client, char *out, size_t want, uint32_t timeoutMs) {
  size_t got = 0;
  const uint32_t start = millis();

  while (got < want) {
    const int c = client.read();
    if (c < 0) {
      if (millis() - start >= timeoutMs) return false;
      if (!waitForMore(client)) return false;
      continue;
    }
    out[got++] = static_cast<char>(c);
  }
  return true;
}

size_t readUntilClose(Client &client, char *out, size_t cap, uint32_t timeoutMs) {
  size_t len = 0;
  const uint32_t start = millis();

  while (len + 1 < cap) {
    const int c = client.read();
    if (c < 0) {
      if (millis() - start >= timeoutMs) break;
      if (!waitForMore(client)) break;
      continue;
    }
    out[len++] = static_cast<char>(c);
  }

  out[len] = '\0';
  return len;
}

void drainBuffered(Client &client, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (client.available() > 0 && millis() - start < timeoutMs) {
    client.read();
  }
}

}  // namespace net
