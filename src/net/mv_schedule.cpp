#include "net/mv_schedule.h"

#include <cstdio>
#include <cstring>

namespace mv {
namespace {

// Field order matches the query the firmware has always sent. It is not
// cosmetic: the parser scans forward for each field in turn, and on a response
// truncated by a full buffer the fields ordered first are the ones that still
// parse. Cheapest and most important first, so what survives is what matters.
struct TopicName {
  uint8_t bit;
  const char *field;
};

constexpr TopicName kTopicNames[] = {
    {kTrackStatus, "TrackStatus"},
    {kLapCount, "LapCount"},
    {kDriverList, "DriverList"},
    {kRaceControlMessages, "RaceControlMessages"},
};

constexpr char kPrefix[] = "{\"query\":\"{f1LiveTimingState{";
constexpr char kSuffix[] = "}}\"}";

}  // namespace

PollPlan planPoll(const PollTiming &timing, const PollState &state, uint32_t nowMs) {
  // Unsigned subtraction throughout, so every interval stays correct across the
  // rollover of a 32-bit millisecond clock.
  const uint32_t due = state.connected ? timing.fastIntervalMs : timing.backoffMs;
  if (state.everPolled && nowMs - state.lastPollMs < due) return {false, 0};

  // Nothing has been seen yet, or the last attempt failed and whatever is in
  // the session state is already suspect. Ask for the lot.
  if (!state.everPolled || !state.connected) return {true, kAllTopics};

  if (timing.slowTopics == 0) return {true, timing.fastTopics};

  if (nowMs - state.lastSlowPollMs >= timing.slowIntervalMs) {
    return {true, static_cast<uint8_t>(timing.fastTopics | timing.slowTopics)};
  }
  return {true, timing.fastTopics};
}

size_t buildQuery(uint8_t topics, char *out, size_t cap) {
  if (topics == 0 || out == nullptr) return 0;

  size_t len = 0;
  const size_t prefixLen = sizeof(kPrefix) - 1;
  const size_t suffixLen = sizeof(kSuffix) - 1;
  if (cap < prefixLen + suffixLen + 1) return 0;

  memcpy(out, kPrefix, prefixLen);
  len = prefixLen;

  bool first = true;
  for (const TopicName &topic : kTopicNames) {
    if ((topics & topic.bit) == 0) continue;

    const size_t fieldLen = strlen(topic.field);
    const size_t needed = fieldLen + (first ? 0 : 1);  // fields are space-separated
    if (len + needed + suffixLen + 1 > cap) return 0;

    if (!first) out[len++] = ' ';
    memcpy(out + len, topic.field, fieldLen);
    len += fieldLen;
    first = false;
  }

  memcpy(out + len, kSuffix, suffixLen);
  len += suffixLen;
  out[len] = '\0';
  return len;
}

}  // namespace mv
