#pragma once

#include <cstddef>
#include <cstdint>

// When to poll MultiViewer and what to ask for. Pure arithmetic over a clock
// the caller supplies, so it is decided here and tested on the host rather
// than being tangled up in the socket code.
//
// The M4 has always asked for everything on a fixed 2s interval, and still
// does -- that is what a schedule with no slow topics comes out as. The split
// exists for the S3, which holds its connection open and can afford to ask for
// the cheap fields often and the expensive ones rarely.
namespace mv {

// The fields the query can ask for. A bitmask rather than a two-level enum
// because which fields travel together is a wiring decision, not a property of
// the feed: the composition root groups them, and this file only counts time.
enum Topic : uint8_t {
  kTrackStatus = 1U << 0U,
  kLapCount = 1U << 1U,
  kDriverList = 1U << 2U,
  kRaceControlMessages = 1U << 3U,
};

constexpr uint8_t kAllTopics = kTrackStatus | kLapCount | kDriverList | kRaceControlMessages;

// Plain aggregates, no default member initializers: the Arduino core builds as
// C++11, where an in-class initializer stops a struct being an aggregate.
struct PollTiming {
  // Between polls of `fastTopics`, once a poll has succeeded.
  uint32_t fastIntervalMs;
  // Between the polls that additionally carry `slowTopics`. Ignored when
  // `slowTopics` is empty.
  uint32_t slowIntervalMs;
  // Used in place of fastIntervalMs while disconnected. Deliberately long: a
  // failed connect() blocks inside the socket layer (~10s on the M4's NINA,
  // ~3s on the S3), and this runs on the same task as rendering until phase 4,
  // so retrying faster than the block lasts would pin the loop.
  uint32_t backoffMs;

  uint8_t fastTopics;
  // Empty means "there is no slow group": every poll asks for fastTopics and
  // slowIntervalMs is never consulted. That is the M4.
  uint8_t slowTopics;
};

struct PollState {
  bool everPolled;
  bool connected;
  uint32_t lastPollMs;
  // When the slow topics were last asked for. Only read when slowTopics is
  // non-empty.
  uint32_t lastSlowPollMs;
};

struct PollPlan {
  bool due;
  // Which topics to request. Meaningful only when `due`.
  uint8_t topics;
};

// Decides whether a poll is due now and what it should carry.
//
// The first poll, and the first after a failure, always asks for everything:
// there is no state worth preserving at that point, and the display needs the
// whole picture before it can show anything true.
PollPlan planPoll(const PollTiming &timing, const PollState &state, uint32_t nowMs);

// Writes the GraphQL query body for `topics` into `out`, NUL-terminated.
// Returns the length written, or 0 if it would not fit or no topic was asked
// for -- either of which is a wiring mistake rather than a runtime condition.
//
// Here rather than in the transport because the request and the schedule that
// chose it are one decision, and this way both are checked by the same test.
size_t buildQuery(uint8_t topics, char *out, size_t cap);

}  // namespace mv
