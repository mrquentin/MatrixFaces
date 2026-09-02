#pragma once

#include <cstddef>
#include <cstdint>

// The parsing half of the MultiViewer client, with no transport and no Arduino
// dependency, so it can be exercised on the host against captured responses.
//
// MultiViewer doesn't publish a schema for this API -- the field names come
// from the raw F1 SignalR live-timing feed that it passes through verbatim
// (topics like TrackStatus, RaceControlMessages), reverse-engineered by the
// community (fastf1, openf1, and others), not from any spec MultiViewer itself
// provides. Every field is therefore treated as optional: a missing or
// unrecognised value degrades to "unknown" rather than a guess, so schema
// drift on MultiViewer's end blanks a field instead of showing something wrong.
//
// That tolerance is also the hazard -- a feed that stopped parsing looks
// exactly like a quiet session -- which is what Counters below exists to make
// visible.
namespace mv {

enum class Flag : uint8_t {
  kUnknown,
  kAllClear,
  kYellow,
  kSafetyCar,
  kVirtualSafetyCar,
  kRed,
};

constexpr size_t kTlaCap = 4;  // 3-letter driver acronym + terminator

// Bounds the RaceControlMessages/DriverList dict scans; matches the largest F1
// grid plus reserve/test entries with generous headroom.
constexpr uint8_t kMaxDrivers = 28;

// A blue flag is re-issued repeatedly while it applies rather than having a
// reliable single clearing event, so an explicit CLEAR message removes it
// immediately and this timeout is just a safety net against a missed one.
constexpr uint32_t kBlueFlagTimeoutMs = 20000;

struct DriverEntry {
  uint16_t racingNumber = 0;  // 0 == unused slot
  char tla[kTlaCap] = {};
};

struct BlueFlagEntry {
  uint16_t racingNumber = 0;  // 0 == unused slot
  uint32_t lastSeenMs = 0;
};

// Everything derived from the feed, plus the carry-over a poll needs from the
// one before it (message high-water mark, blue-flag ages).
struct SessionState {
  Flag trackFlag = Flag::kUnknown;

  bool hasLapCount = false;
  uint32_t currentLap = 0;
  uint32_t totalLaps = 0;

  DriverEntry drivers[kMaxDrivers];

  int32_t lastMessageIndex = -1;
  BlueFlagEntry blueFlags[kMaxDrivers];
  char blueFlagTla[kTlaCap] = {};

  bool hasBlueFlag() const { return blueFlagTla[0] != '\0'; }

  // Back to "no session", used when the feed says so explicitly.
  void reset();

  const char *tlaForRacingNumber(uint16_t racingNumber) const;
};

// Why polls and parses are ending. Reported through /api/metrics: without it,
// a feed that has quietly stopped parsing is indistinguishable from a session
// where nothing is happening.
struct Counters {
  // Transport, maintained by MultiViewerClient.
  uint32_t polls = 0;
  uint32_t connectFailures = 0;
  uint32_t emptyResponses = 0;
  uint32_t framingErrors = 0;  // no CRLFCRLF, i.e. not an HTTP response

  // Parsing, maintained here.
  uint32_t parsed = 0;     // a live session object was walked
  uint32_t noSession = 0;  // f1LiveTimingState was null
  uint32_t malformed = 0;  // absent, or not an object
  uint32_t truncated = 0;  // object ran off the end of the buffer
};

enum class ParseResult : uint8_t {
  kOk,
  kNoSession,
  kMalformed,
  kTruncated,
};

// Parses one response body into `state`. `buf` is mutated in place: the
// scanner temporarily NUL-terminates at scope boundaries so plain strstr can be
// reused instead of hand-rolling a bounded search, so the caller must pass a
// writable buffer and must not rely on its contents afterwards.
ParseResult parse(char *buf, size_t len, uint32_t nowMs, SessionState &state, Counters &counters);

}  // namespace mv
