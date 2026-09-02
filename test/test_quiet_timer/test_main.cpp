#include <unity.h>

#include "storage/quiet_timer.h"

// The debounce that decides when settings reach flash. Every case here is one
// the board would take seconds to reproduce and a rollover case it would take
// seven weeks, which is the reason this takes its clock as an argument.
//
// The state is a single atomic word rather than a flag plus a timestamp,
// because it is marked on one core and read on another. Two fields were read
// half-updated on hardware and produced three writes where there should have
// been one.

constexpr uint32_t kQuiet = 5000;

void setUp() {}
void tearDown() {}

void test_nothing_to_write_is_never_due() {
  QuietTimer timer(kQuiet);
  TEST_ASSERT_FALSE(timer.pending());
  TEST_ASSERT_FALSE(timer.due(0));
  TEST_ASSERT_FALSE(timer.due(1000000));
}

void test_a_change_is_pending_immediately_but_not_due() {
  QuietTimer timer(kQuiet);
  timer.mark(1000);
  TEST_ASSERT_TRUE(timer.pending());
  TEST_ASSERT_FALSE(timer.due(1000));
  TEST_ASSERT_FALSE(timer.due(1000 + kQuiet - 1));
}

void test_due_exactly_on_the_quiet_period() {
  QuietTimer timer(kQuiet);
  timer.mark(1000);
  TEST_ASSERT_TRUE(timer.due(1000 + kQuiet));
}

// The point of the whole thing: a slider dragged across a colour picker is one
// write after the user stops, not one per frame.
void test_a_burst_of_changes_writes_once_after_the_last() {
  QuietTimer timer(kQuiet);
  for (uint32_t t = 0; t <= 4000; t += 100) {
    timer.mark(t);
    TEST_ASSERT_FALSE(timer.due(t));
  }
  // Still not due 4.9s after the burst started, because it ended at 4000.
  TEST_ASSERT_FALSE(timer.due(4000 + kQuiet - 1));
  TEST_ASSERT_TRUE(timer.due(4000 + kQuiet));
}

void test_clearing_stops_it_being_due() {
  QuietTimer timer(kQuiet);
  timer.mark(1000);
  TEST_ASSERT_TRUE(timer.due(9000));
  timer.clear();
  TEST_ASSERT_FALSE(timer.pending());
  TEST_ASSERT_FALSE(timer.due(9000));
}

void test_a_change_after_clearing_starts_a_new_period() {
  QuietTimer timer(kQuiet);
  timer.mark(1000);
  timer.clear();
  timer.mark(20000);
  TEST_ASSERT_FALSE(timer.due(20000 + kQuiet - 1));
  TEST_ASSERT_TRUE(timer.due(20000 + kQuiet));
}

// Without unsigned arithmetic this would either fire instantly or never again,
// depending on which way the subtraction went. Seven weeks of uptime is not a
// thing to discover in the field.
void test_the_quiet_period_survives_the_millis_rollover() {
  QuietTimer timer(kQuiet);
  const uint32_t beforeWrap = 0xFFFFF000u;
  timer.mark(beforeWrap);

  TEST_ASSERT_FALSE(timer.due(beforeWrap + kQuiet - 1));
  TEST_ASSERT_TRUE(timer.due(beforeWrap + kQuiet));
}

// A quiet period of zero means "write on the next pass", which is what a
// board with nothing to gain from batching would ask for.
void test_a_zero_quiet_period_is_due_at_once() {
  QuietTimer timer(0);
  timer.mark(1234);
  TEST_ASSERT_TRUE(timer.due(1234));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_to_write_is_never_due);
  RUN_TEST(test_a_change_is_pending_immediately_but_not_due);
  RUN_TEST(test_due_exactly_on_the_quiet_period);
  RUN_TEST(test_a_burst_of_changes_writes_once_after_the_last);
  RUN_TEST(test_clearing_stops_it_being_due);
  RUN_TEST(test_a_change_after_clearing_starts_a_new_period);
  RUN_TEST(test_the_quiet_period_survives_the_millis_rollover);
  RUN_TEST(test_a_zero_quiet_period_is_due_at_once);
  return UNITY_END();
}
