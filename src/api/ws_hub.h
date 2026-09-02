#pragma once

#include <cstddef>
#include <cstdint>

#include "api/websocket.h"
#include "board/net_link.h"

// The open WebSocket connections, and the only thing that touches them.
//
// Owned solely by the network task. Nothing else reads or writes a connection:
// broadcasts are handed here and sent from the same task that reads, so there
// is no lock and no half-written frame. That is the rule phase 5.2 has to
// respect when it starts fanning events out.
//
// Small on purpose. Four connections, one partial frame each, no
// fragmentation, no extensions. A browser tab and a couple of tools is what
// this is for.
class WsHub {
 public:
  static constexpr uint8_t kMaxConnections = net_link::kMaxRetained;

  // Longest inbound message worth accepting -- a settings change is well under
  // a hundred bytes. Anything larger is closed rather than buffered, because a
  // client that sends it is not one of ours.
  static constexpr size_t kInboundCap = 256;

  // Called for each complete text message, on the network task. `json` is
  // NUL-terminated.
  using MessageHandler = void (*)(const char *json, size_t len, void *user);

  WsHub(MessageHandler handler, void *user) : handler_(handler), user_(user) {}

  // Takes over a connection the router has already authenticated and upgraded.
  // `slot` comes from net_link::retain(). False if the table is full, in which
  // case the caller must release the slot.
  bool adopt(int8_t slot);

  // Reads whatever has arrived on every connection and acts on it. Never
  // blocks: a message that has not fully arrived stays buffered for the next
  // call, which is what lets one slow peer not hold up the others.
  void poll();

  // Sends a text frame to every connection. Best effort -- a connection that
  // will not take it is closed rather than retried, since the alternative is
  // blocking the task that serves everyone else.
  void broadcast(const char *text, size_t len);

  uint8_t count() const;

  // Total messages accepted across all connections since boot, for
  // /api/metrics. The plan's per-connection counter turned out to be less
  // useful than this: connections are anonymous once authenticated, and what
  // anyone debugging wants to know is whether traffic is flowing at all.
  uint32_t messagesReceived() const { return messagesReceived_; }

 private:
  struct Connection {
    // -1 when the entry is free.
    int8_t slot;
    size_t len;
    uint8_t buf[kInboundCap];
  };

  void closeConnection(Connection &connection, uint16_t code);
  // Consumes as many whole frames as `connection.buf` holds. False if the
  // connection was closed while doing so.
  bool drainFrames(Connection &connection);

  MessageHandler handler_;
  void *user_;
  Connection connections_[kMaxConnections] = {};
  bool initialised_ = false;
  uint32_t messagesReceived_ = 0;
};
