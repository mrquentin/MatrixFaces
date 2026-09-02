#include <unity.h>

#include <cstring>

#include "net/mv_schedule.h"

// The poll schedule, checked without a clock or a socket. Every case here is
// one the board would otherwise take minutes to reproduce -- a rollover, a
// backoff, the first poll after a failure -- which is the reason this is a
// pure function at all.

namespace {

// The M4's wiring: everything, every two seconds, no slow group.
mv::PollTiming m4Timing() { return {2000, 2000, 30000, mv::kAllTopics, 0}; }

// The S3's: a cheap group often, an expensive one occasionally.
mv::PollTiming splitTiming() {
  return {2000, 12000, 5000, mv::kTrackStatus | mv::kLapCount,
          mv::kDriverList | mv::kRaceControlMessages};
}

mv::PollState settled(uint32_t lastPollMs, uint32_t lastSlowPollMs) {
  return {true, true, lastPollMs, lastSlowPollMs};
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- when a poll is due ----------------------------------------------------

void test_first_poll_is_due_immediately() {
  const mv::PollPlan plan = mv::planPoll(m4Timing(), {false, false, 0, 0}, 0);
  TEST_ASSERT_TRUE(plan.due);
}

void test_first_poll_asks_for_everything() {
  const mv::PollPlan plan = mv::planPoll(splitTiming(), {false, false, 0, 0}, 0);
  TEST_ASSERT_TRUE(plan.due);
  TEST_ASSERT_EQUAL_UINT8(mv::kAllTopics, plan.topics);
}

void test_not_due_before_the_interval() {
  const mv::PollPlan plan = mv::planPoll(m4Timing(), settled(1000, 1000), 2999);
  TEST_ASSERT_FALSE(plan.due);
}

void test_due_exactly_on_the_interval() {
  const mv::PollPlan plan = mv::planPoll(m4Timing(), settled(1000, 1000), 3000);
  TEST_ASSERT_TRUE(plan.due);
}

// While disconnected the long backoff applies, not the fast interval: a failed
// connect() blocks the caller for seconds, so retrying on the fast interval
// would pin the loop.
void test_disconnected_waits_for_the_backoff() {
  const mv::PollState disconnected = {true, false, 1000, 1000};
  TEST_ASSERT_FALSE(mv::planPoll(m4Timing(), disconnected, 10000).due);
  TEST_ASSERT_TRUE(mv::planPoll(m4Timing(), disconnected, 31000).due);
}

void test_poll_after_a_failure_asks_for_everything() {
  const mv::PollState disconnected = {true, false, 1000, 1000};
  const mv::PollPlan plan = mv::planPoll(splitTiming(), disconnected, 31000);
  TEST_ASSERT_TRUE(plan.due);
  TEST_ASSERT_EQUAL_UINT8(mv::kAllTopics, plan.topics);
}

// The intervals are unsigned differences, so a clock that has wrapped past
// 2^32 is just a small difference again rather than an enormous one.
void test_intervals_survive_the_millis_rollover() {
  const uint32_t beforeWrap = 0xFFFFF000u;
  const mv::PollState state = settled(beforeWrap, beforeWrap);

  TEST_ASSERT_FALSE(mv::planPoll(m4Timing(), state, beforeWrap + 1999).due);
  TEST_ASSERT_TRUE(mv::planPoll(m4Timing(), state, beforeWrap + 2000).due);
}

// --- which topics a poll carries -------------------------------------------

// With no slow group every poll asks for everything, which is what the M4 has
// always done and must keep doing.
void test_no_slow_group_always_asks_for_everything() {
  for (uint32_t now = 3000; now < 60000; now += 2000) {
    const mv::PollPlan plan = mv::planPoll(m4Timing(), settled(now - 2000, 0), now);
    TEST_ASSERT_TRUE(plan.due);
    TEST_ASSERT_EQUAL_UINT8(mv::kAllTopics, plan.topics);
  }
}

void test_fast_poll_omits_the_slow_topics() {
  const mv::PollPlan plan = mv::planPoll(splitTiming(), settled(10000, 10000), 12000);
  TEST_ASSERT_TRUE(plan.due);
  TEST_ASSERT_EQUAL_UINT8(mv::kTrackStatus | mv::kLapCount, plan.topics);
}

void test_slow_topics_fold_in_once_their_interval_passes() {
  const mv::PollPlan plan = mv::planPoll(splitTiming(), settled(10000, 0), 12000);
  TEST_ASSERT_TRUE(plan.due);
  TEST_ASSERT_EQUAL_UINT8(mv::kAllTopics, plan.topics);
}

// --- the query the plan turns into -----------------------------------------

void test_query_for_all_topics_matches_the_original() {
  // Byte-for-byte what the firmware sent before the schedule existed. The M4
  // asks for exactly this, so a change here is a change to its behaviour.
  char out[160];
  const size_t len = mv::buildQuery(mv::kAllTopics, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(
      "{\"query\":\"{f1LiveTimingState{TrackStatus LapCount DriverList "
      "RaceControlMessages}}\"}",
      out);
  TEST_ASSERT_EQUAL_size_t(strlen(out), len);
}

void test_query_for_a_subset_lists_only_those_fields() {
  char out[160];
  mv::buildQuery(mv::kTrackStatus | mv::kLapCount, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("{\"query\":\"{f1LiveTimingState{TrackStatus LapCount}}\"}", out);
}

// Field order is the parser's truncation order, not the caller's bit order.
void test_query_field_order_is_fixed() {
  char out[160];
  mv::buildQuery(mv::kRaceControlMessages | mv::kTrackStatus, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(
      "{\"query\":\"{f1LiveTimingState{TrackStatus RaceControlMessages}}\"}", out);
}

void test_query_refuses_an_empty_topic_set() {
  char out[160];
  TEST_ASSERT_EQUAL_size_t(0, mv::buildQuery(0, out, sizeof(out)));
}

// Refuses rather than emitting a query missing the fields that did not fit,
// which would look like a working poll returning nothing.
void test_query_refuses_a_buffer_it_would_overrun() {
  char out[40];
  TEST_ASSERT_EQUAL_size_t(0, mv::buildQuery(mv::kAllTopics, out, sizeof(out)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_poll_is_due_immediately);
  RUN_TEST(test_first_poll_asks_for_everything);
  RUN_TEST(test_not_due_before_the_interval);
  RUN_TEST(test_due_exactly_on_the_interval);
  RUN_TEST(test_disconnected_waits_for_the_backoff);
  RUN_TEST(test_poll_after_a_failure_asks_for_everything);
  RUN_TEST(test_intervals_survive_the_millis_rollover);
  RUN_TEST(test_no_slow_group_always_asks_for_everything);
  RUN_TEST(test_fast_poll_omits_the_slow_topics);
  RUN_TEST(test_slow_topics_fold_in_once_their_interval_passes);
  RUN_TEST(test_query_for_all_topics_matches_the_original);
  RUN_TEST(test_query_for_a_subset_lists_only_those_fields);
  RUN_TEST(test_query_field_order_is_fixed);
  RUN_TEST(test_query_refuses_an_empty_topic_set);
  RUN_TEST(test_query_refuses_a_buffer_it_would_overrun);
  return UNITY_END();
}
