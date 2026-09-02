#pragma once

#include <Client.h>
#include <IPAddress.h>

#include <cstddef>
#include <cstdint>

#include "net/multiviewer_parse.h"
#include "net/mv_schedule.h"

// How this board talks to MultiViewer. Supplied by the composition root
// because the right answer differs per board and is not the client's to guess.
//
// The M4 keeps what it has always done: connect, ask for everything, read
// until the peer closes, every two seconds. Its socket layer blocks for ~10s
// on a failed connect and it has 32 KB to hold a response in, so nothing here
// is worth trading.
//
// The S3 holds the connection open and splits the query, because it can: a
// reused socket costs no handshake, and the fields that matter every two
// seconds are far smaller than the one that grows all session.
//
// Plain aggregate, no default member initializers, as elsewhere in this
// codebase: the Arduino core builds as C++11, where an in-class initializer
// stops a struct being an aggregate at all.
struct MvConfig {
  mv::PollTiming timing;

  // Whether to ask the server to hold the connection open between polls, and
  // to reuse it when it does. Reading a response then needs real framing
  // (a Content-Length) rather than "read until the peer hangs up"; when the
  // server does not supply one this falls back to closing, so the setting is a
  // preference rather than a requirement.
  bool keepAlive;

  // How long to wait on a response that has gone quiet. Not a cap on the whole
  // read: a peer that keeps producing bytes is read to completion however long
  // it takes (see net/stream_read.h).
  uint32_t responseTimeoutMs;
};

// Transport for MultiViewer's (https://multiviewer.app/) local GraphQL API at
// http://<host>:10101/api/graphql: connect, POST the query, read the response,
// hand the body to mv::parse().
//
// Everything about interpreting the feed lives in net/multiviewer_parse, and
// everything about *when* to poll and what to ask for lives in net/mv_schedule
// -- both pure, both tested on the host. This class is only the part that
// needs a socket.
class MultiViewerClient {
 public:
  // Kept as member aliases so callers (F1FlagsApp) read naturally and did not
  // have to change when the parsing moved out.
  using Flag = mv::Flag;
  static constexpr size_t kTlaCap = mv::kTlaCap;

  // `transport` is borrowed, not owned. It must not be shared with another
  // consumer: connecting tears down whatever connection the instance was
  // already holding, and with keep-alive this one expects to still be holding
  // its own between polls.
  //
  // `buffer` comes from board/bigbuf.h via the composition root. At 32-64 KB
  // it is by far the largest allocation in the firmware, and which memory it
  // can live in is a board question.
  MultiViewerClient(Client &transport, char *buffer, size_t bufferCap, const MvConfig &config)
      : transport_(transport), buffer_(buffer), bufferCap_(bufferCap), config_(config) {}

  // Host to poll; port 10101 is MultiViewer's fixed local API port. An unset
  // (all-zero) address means "not configured" and poll() is then a no-op.
  void setHost(IPAddress host);
  IPAddress host() const { return host_; }

  // Call every loop() iteration. What it does when is mv::planPoll's decision.
  void poll(uint32_t nowMs);

  // True once a poll has round-tripped successfully; flips back to false the
  // moment one fails, so the app can fall back to a connection-status screen
  // instead of showing stale data.
  bool connected() const { return connected_; }

  // Why polls are succeeding or failing. Surfaced through /api/metrics: a feed
  // that has quietly stopped parsing otherwise looks like a quiet session.
  const mv::Counters &counters() const { return counters_; }
  const mv::SessionState &state() const { return state_; }

  // Current track-wide flag. MultiViewer/F1 already resolve this to a single
  // value, so no extra prioritisation is needed here: it's never both
  // "Yellow" and "SCDeployed" at once.
  Flag trackFlag() const { return state_.trackFlag; }

  bool hasLapCount() const { return state_.hasLapCount; }
  uint32_t currentLap() const { return state_.currentLap; }
  uint32_t totalLaps() const { return state_.totalLaps; }

  // A driver-scoped blue flag, if one is currently active. Deliberately not
  // gated on trackFlag() here -- see f1_flags_app.cpp for the display
  // priority (blue is suppressed whenever another flag is showing).
  bool hasBlueFlag() const { return state_.hasBlueFlag(); }
  const char *blueFlagTla() const { return state_.blueFlagTla; }

 private:
  static constexpr uint16_t kPort = 10101;
  // Comfortably over the longest query buildQuery can produce.
  static constexpr size_t kQueryCap = 160;
  static constexpr size_t kHeaderLineCap = 256;
  // Generous next to the handful MultiViewer sends, and small enough that a
  // peer emitting headers forever is cut off rather than read forever.
  static constexpr size_t kMaxHeaderLines = 32;

  // Opens a connection, or keeps the one already open when configured to.
  // False if a new connection could not be made.
  bool ensureConnected();

  // Sends the query for `topics`. False if the request could not be written.
  bool sendRequest(uint8_t topics);

  // Reads status line, headers and body into buffer_. `outLen` is the body
  // length. Sets `reusable` to whether the connection may serve another
  // request afterwards.
  bool readResponse(size_t &outLen, bool &reusable);

  // One request/response round trip, reconnecting once if a *reused*
  // connection turned out to be dead. That retry is the whole reason
  // keep-alive is safe to use against a server that drops idle sockets
  // without telling us.
  bool fetch(uint8_t topics, size_t &outLen);

  Client &transport_;
  char *buffer_;
  size_t bufferCap_;
  MvConfig config_;

  IPAddress host_;
  bool everPolled_ = false;
  bool connected_ = false;
  bool socketOpen_ = false;
  uint32_t lastPollMs_ = 0;
  uint32_t lastSlowPollMs_ = 0;

  mv::SessionState state_;
  mv::Counters counters_;
};
