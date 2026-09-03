#include "api/ws_hub.h"

#include <Arduino.h>

#include <cstring>

namespace {

// RFC 6455 close codes, the three this uses.
constexpr uint16_t kCloseNormal = 1000;
constexpr uint16_t kCloseProtocolError = 1002;
constexpr uint16_t kCloseTooBig = 1009;

}  // namespace

bool WsHub::adopt(int8_t slot) {
  if (slot < 0) return false;

  rtos::LockGuard guard(mutex_);

  // Connection is a plain aggregate so the table can be zero-initialised, but
  // zero is a valid slot index -- so the free marker has to be written once
  // rather than assumed.
  if (!initialised_) {
    for (Connection &connection : connections_) connection.slot = -1;
    initialised_ = true;
  }

  for (Connection &connection : connections_) {
    if (connection.slot >= 0) continue;
    connection.slot = slot;
    connection.len = 0;
    return true;
  }
  return false;
}

uint8_t WsHub::count() const {
  uint8_t open = 0;
  for (const Connection &connection : connections_) {
    if (connection.slot >= 0) ++open;
  }
  return open;
}

void WsHub::closeConnection(Connection &connection, uint16_t code) {
  Client *client = net_link::retained(static_cast<uint8_t>(connection.slot));
  if (client != nullptr) {
    uint8_t frame[ws::kMaxControlFrame];
    const size_t len = ws::encodeClose(frame, sizeof(frame), code);
    if (len > 0) client->write(frame, len);
  }
  net_link::release(static_cast<uint8_t>(connection.slot));
  connection.slot = -1;
  connection.len = 0;
}

bool WsHub::drainFrames(Connection &connection) {
  while (true) {
    ws::FrameHeader header{};
    const ws::ParseResult result = ws::parseHeader(connection.buf, connection.len, header);

    if (result == ws::ParseResult::kIncomplete) return true;  // wait for more
    if (result == ws::ParseResult::kUnsupported) {
      closeConnection(connection, kCloseProtocolError);
      return false;
    }

    // Every frame from a client must be masked; an unmasked one is either a
    // broken client or something that is not a browser.
    if (!header.masked) {
      closeConnection(connection, kCloseProtocolError);
      return false;
    }

    if (header.payloadLen > kInboundCap - header.headerLen) {
      closeConnection(connection, kCloseTooBig);
      return false;
    }

    const size_t total = header.headerLen + static_cast<size_t>(header.payloadLen);
    if (connection.len < total) return true;  // payload still arriving

    uint8_t *payload = connection.buf + header.headerLen;
    const auto payloadLen = static_cast<size_t>(header.payloadLen);
    ws::unmask(payload, payloadLen, header.maskKey);

    switch (header.opcode) {
      case ws::Opcode::kText: {
        // NUL-terminated in place: the frame header in front of it is no
        // longer needed, and the byte after the payload is either the next
        // frame (about to be shifted down) or free space.
        const uint8_t terminated = connection.buf[total];
        payload[payloadLen] = '\0';
        ++messagesReceived_;
        if (handler_ != nullptr) {
          handler_(reinterpret_cast<const char *>(payload), payloadLen, user_);
        }
        connection.buf[total] = terminated;
        break;
      }

      case ws::Opcode::kPing: {
        Client *client = net_link::retained(static_cast<uint8_t>(connection.slot));
        if (client != nullptr) {
          uint8_t frame[ws::kMaxControlFrame];
          const size_t len = ws::encodePong(frame, sizeof(frame), payload, payloadLen);
          if (len > 0) client->write(frame, len);
        }
        break;
      }

      case ws::Opcode::kClose:
        closeConnection(connection, kCloseNormal);
        return false;

      case ws::Opcode::kPong:
        break;  // unsolicited pongs are allowed and mean nothing here

      case ws::Opcode::kBinary:
      case ws::Opcode::kContinuation:
        // Neither is produced by anything that talks to this board, and
        // accepting them would mean implementing fragmentation.
        closeConnection(connection, kCloseProtocolError);
        return false;
    }

    // Shift the remainder down. Messages are small and arrive one at a time,
    // so this is a memmove of almost nothing in practice.
    connection.len -= total;
    if (connection.len > 0) memmove(connection.buf, connection.buf + total, connection.len);
  }
}

void WsHub::poll() {
  rtos::LockGuard guard(mutex_);
  for (Connection &connection : connections_) {
    if (connection.slot < 0) continue;

    Client *client = net_link::retained(static_cast<uint8_t>(connection.slot));
    if (client == nullptr || !client->connected()) {
      net_link::release(static_cast<uint8_t>(connection.slot));
      connection.slot = -1;
      connection.len = 0;
      continue;
    }

    // Only what has already arrived. available() never blocks, which is what
    // keeps one silent peer from holding up the rest -- and what makes this
    // safe to call from the same tick that accepts HTTP.
    while (client->available() > 0 && connection.len < kInboundCap) {
      const int byte = client->read();
      if (byte < 0) break;
      connection.buf[connection.len++] = static_cast<uint8_t>(byte);
    }

    if (connection.len >= kInboundCap) {
      // Filled without a complete frame: the peer is sending something this
      // was never meant to carry.
      closeConnection(connection, kCloseTooBig);
      continue;
    }

    drainFrames(connection);
  }
}

void WsHub::broadcast(const char *text, size_t len) {
  if (text == nullptr || len == 0) return;

  rtos::LockGuard guard(mutex_);

  uint8_t header[ws::kMaxServerHeader];
  const size_t headerLen = ws::encodeHeader(header, sizeof(header), ws::Opcode::kText, len);
  if (headerLen == 0) return;

  for (Connection &connection : connections_) {
    if (connection.slot < 0) continue;

    Client *client = net_link::retained(static_cast<uint8_t>(connection.slot));
    if (client == nullptr || !client->connected()) {
      net_link::release(static_cast<uint8_t>(connection.slot));
      connection.slot = -1;
      connection.len = 0;
      continue;
    }

    // Header and payload separately so a large message is not copied twice.
    // A short write means the peer is not keeping up; dropping it is better
    // than blocking every other connection behind it.
    if (client->write(header, headerLen) != headerLen ||
        client->write(reinterpret_cast<const uint8_t *>(text), len) != len) {
      net_link::release(static_cast<uint8_t>(connection.slot));
      connection.slot = -1;
      connection.len = 0;
    }
  }
}
