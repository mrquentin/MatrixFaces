#pragma once

#include <IPAddress.h>

#include <cstddef>
#include <cstdint>

// Polls MultiViewer's (https://multiviewer.app/) local GraphQL API at
// http://<host>:10101/api/graphql for F1 flag/lap state.
//
// MultiViewer doesn't publish a schema for this API -- the field names below
// come from the raw F1 SignalR live-timing feed that it passes through
// verbatim (topics like TrackStatus, RaceControlMessages), reverse-engineered
// by the community (fastf1, openf1, and others), not from any spec
// MultiViewer itself provides. Every field is therefore treated as optional:
// a missing or unrecognised value degrades to "unknown" rather than a guess,
// so schema drift on MultiViewer's end blanks a field instead of showing
// something wrong.
class MultiViewerClient {
 public:
  enum class Flag : uint8_t {
    kUnknown,
    kAllClear,
    kYellow,
    kSafetyCar,
    kVirtualSafetyCar,
    kRed,
  };

  static constexpr size_t kTlaCap = 4;  // 3-letter driver acronym + terminator

  // Host to poll; port 10101 is MultiViewer's fixed local API port. An unset
  // (all-zero) address means "not configured" and poll() is then a no-op.
  void setHost(IPAddress host);
  IPAddress host() const { return host_; }

  // Call every loop() iteration. Throttles itself to kPollIntervalMs and is a
  // no-op when the host is unset. An actual poll blocks for up to a couple of
  // seconds (WiFiClient connect + read) -- the same tradeoff TimezoneOffset
  // makes elsewhere in this firmware, acceptable here because it only runs
  // while the F1 flags app is the active one.
  void poll(uint32_t nowMs);

  // True once a poll has round-tripped successfully; flips back to false the
  // moment one fails, so the app can fall back to a connection-status screen
  // instead of showing stale data.
  bool connected() const { return connected_; }

  // Current track-wide flag. MultiViewer/F1 already resolve this to a single
  // value, so no extra prioritisation is needed here: it's never both
  // "Yellow" and "SCDeployed" at once.
  Flag trackFlag() const { return trackFlag_; }

  bool hasLapCount() const { return hasLapCount_; }
  uint32_t currentLap() const { return currentLap_; }
  uint32_t totalLaps() const { return totalLaps_; }

  // A driver-scoped blue flag, if one is currently active. Deliberately not
  // gated on trackFlag() here -- see f1_flags_app.cpp for the display
  // priority (blue is suppressed whenever another flag is showing).
  bool hasBlueFlag() const { return blueFlagTla_[0] != '\0'; }
  const char *blueFlagTla() const { return blueFlagTla_; }

 private:
  static constexpr uint16_t kPort = 10101;
  static constexpr uint32_t kPollIntervalMs = 2000;
  static constexpr uint32_t kResponseTimeoutMs = 3000;
  // Sized for TrackStatus/LapCount/DriverList (a few KB combined) plus
  // RaceControlMessages, which grows for the entire
  // session and is the dominant cost -- comfortably covers even a very
  // eventful multi-hour race. If a session ever produces more, only blue-flag
  // detection (the last, largest field in the query) degrades; the smaller
  // fields ordered ahead of it in the query still parse.
  static constexpr size_t kResponseCap = 32768;
  // Bounds the RaceControlMessages/DriverList dict scans; matches the
  // largest F1 grid plus reserve/test entries with generous headroom.
  static constexpr uint8_t kMaxDrivers = 28;
  // A blue flag is re-issued repeatedly while it applies rather than having
  // a reliable single clearing event, so an explicit CLEAR message removes
  // it immediately and this timeout is just a safety net against a missed
  // one.
  static constexpr uint32_t kBlueFlagTimeoutMs = 20000;

  struct BlueFlagEntry {
    uint16_t racingNumber = 0;  // 0 == unused slot
    uint32_t lastSeenMs = 0;
  };

  struct DriverEntry {
    uint16_t racingNumber = 0;  // 0 == unused slot
    char tla[kTlaCap] = {};
  };

  bool fetch(char *buf, size_t cap, size_t &outLen);
  void parseResponse(char *buf, size_t len, uint32_t nowMs);
  void parseTrackStatus(char *scopeStart, char *scopeEnd);
  void parseLapCount(char *scopeStart, char *scopeEnd);
  void parseDriverList(char *scopeStart, char *scopeEnd);
  void parseRaceControlMessages(char *scopeStart, char *scopeEnd, uint32_t nowMs);
  void resetSessionState();
  const char *tlaForRacingNumber(uint16_t racingNumber) const;

  IPAddress host_;
  uint32_t lastPollMs_ = 0;
  bool everPolled_ = false;

  bool connected_ = false;

  Flag trackFlag_ = Flag::kUnknown;

  bool hasLapCount_ = false;
  uint32_t currentLap_ = 0;
  uint32_t totalLaps_ = 0;

  DriverEntry drivers_[kMaxDrivers];

  int32_t lastMessageIndex_ = -1;
  BlueFlagEntry blueFlags_[kMaxDrivers];
  char blueFlagTla_[kTlaCap] = {};
};
