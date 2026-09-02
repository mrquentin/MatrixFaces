#pragma once

#include <Client.h>
#include <IPAddress.h>

#include <cstddef>
#include <cstdint>

#include "net/multiviewer_parse.h"

// Transport for MultiViewer's (https://multiviewer.app/) local GraphQL API at
// http://<host>:10101/api/graphql: connect, POST the query, read the response,
// hand the body to mv::parse().
//
// Everything about interpreting the feed lives in net/multiviewer_parse, which
// has no Arduino dependency and is tested on the host against captured
// responses. This class is only the part that needs a socket.
class MultiViewerClient {
 public:
  // Kept as member aliases so callers (F1FlagsApp) read naturally and did not
  // have to change when the parsing moved out.
  using Flag = mv::Flag;
  static constexpr size_t kTlaCap = mv::kTlaCap;

  // `transport` is borrowed, not owned, and is reconnected on each poll. It
  // must not be shared with another consumer: connecting tears down whatever
  // connection the instance was already holding.
  //
  // `buffer` is the response scratch space, supplied by the composition root
  // because at 32 KB it is by far the largest single allocation in the
  // firmware, and hiding it in a function-local static made that invisible.
  MultiViewerClient(Client &transport, char *buffer, size_t bufferCap)
      : transport_(transport), buffer_(buffer), bufferCap_(bufferCap) {}

  // Host to poll; port 10101 is MultiViewer's fixed local API port. An unset
  // (all-zero) address means "not configured" and poll() is then a no-op.
  void setHost(IPAddress host);
  IPAddress host() const { return host_; }

  // Call every loop() iteration. Throttles retries to kPollIntervalMs once
  // connected, or kReconnectBackoffMs while not (see the .cpp for why: a
  // failed WiFiClient::connect() blocks for up to 10s internally, with no
  // way to shorten that via WiFiNINA's public API, so retrying on anything
  // shorter than that would starve loop() -- and everything else that
  // depends on it, like the HTTP API and button handling -- continuously
  // while the host is unreachable). A no-op when the host is unset.
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
  static constexpr uint32_t kPollIntervalMs = 2000;
  // WiFiClient::connect() blocks up to 10s internally on a failed attempt
  // (see poll()'s doc comment); this must stay comfortably above that so a
  // wrong/unreachable host doesn't pin loop() in back-to-back 10s blocks.
  static constexpr uint32_t kReconnectBackoffMs = 30000;
  static constexpr uint32_t kResponseTimeoutMs = 3000;

  bool fetch(size_t &outLen);

  Client &transport_;
  char *buffer_;
  size_t bufferCap_;

  IPAddress host_;
  uint32_t lastPollMs_ = 0;
  bool everPolled_ = false;
  bool connected_ = false;

  mv::SessionState state_;
  mv::Counters counters_;
};

// Sized for TrackStatus/LapCount/DriverList (a few KB combined) plus
// RaceControlMessages, which grows for the entire session and is the dominant
// cost -- comfortably covers even a very eventful multi-hour race. If a session
// ever produces more, only blue-flag detection (the last, largest field in the
// query) degrades; the smaller fields ordered ahead of it still parse, and the
// `truncated` counter now says so out loud.
constexpr size_t kMvResponseCap = 32768;
