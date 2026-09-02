#include <unity.h>

#include <cstring>

#include "board/native/rtos_native.h"
#include "board/rtos.h"

// The command queue, against the single-task implementation the SAMD51 also
// uses. The S3's is a FreeRTOS queue and cannot be exercised here, so what
// this pins is the *contract* both must honour: order, capacity, the refusal
// when full, and that a posted command is copied rather than referenced.

namespace {

Command switchTo(uint8_t index) {
  Command command{};
  command.kind = Command::Kind::kSwitchApp;
  command.appIndex = index;
  return command;
}

Command applySetting(uint8_t appIndex, const char *key, int32_t intValue) {
  Command command{};
  command.kind = Command::Kind::kApplySetting;
  command.appIndex = appIndex;
  strncpy(command.key, key, sizeof(command.key) - 1);
  command.value.type = SettingType::kInt;
  command.value.intValue = intValue;
  return command;
}

}  // namespace

void setUp() { rtos_native::reset(); }
void tearDown() {}

void test_empty_queue_yields_nothing() {
  Command out{};
  TEST_ASSERT_FALSE(rtos::commandTake(out));
}

void test_a_posted_command_comes_back_intact() {
  TEST_ASSERT_TRUE(rtos::commandPost(applySetting(2, "color", 0x00b4ff)));

  Command out{};
  TEST_ASSERT_TRUE(rtos::commandTake(out));
  TEST_ASSERT_EQUAL(Command::Kind::kApplySetting, out.kind);
  TEST_ASSERT_EQUAL_UINT8(2, out.appIndex);
  TEST_ASSERT_EQUAL_STRING("color", out.key);
  TEST_ASSERT_EQUAL_INT32(0x00b4ff, out.value.intValue);
}

void test_commands_come_back_in_order() {
  for (uint8_t i = 0; i < 4; ++i) TEST_ASSERT_TRUE(rtos::commandPost(switchTo(i)));

  for (uint8_t i = 0; i < 4; ++i) {
    Command out{};
    TEST_ASSERT_TRUE(rtos::commandTake(out));
    TEST_ASSERT_EQUAL_UINT8(i, out.appIndex);
  }
  Command drained{};
  TEST_ASSERT_FALSE(rtos::commandTake(drained));
}

// The poster keeps no ownership: whatever it used to build the command may be
// gone by the time the other task drains it.
void test_the_command_is_copied_not_referenced() {
  char key[16] = "size";
  Command command{};
  command.kind = Command::Kind::kApplySetting;
  strncpy(command.key, key, sizeof(command.key) - 1);
  TEST_ASSERT_TRUE(rtos::commandPost(command));

  memset(key, 0, sizeof(key));
  memset(&command, 0, sizeof(command));

  Command out{};
  TEST_ASSERT_TRUE(rtos::commandTake(out));
  TEST_ASSERT_EQUAL_STRING("size", out.key);
}

void test_queue_fills_and_then_refuses() {
  for (uint8_t i = 0; i < rtos::kCommandQueueDepth; ++i) {
    TEST_ASSERT_TRUE(rtos::commandPost(switchTo(i)));
  }
  TEST_ASSERT_FALSE(rtos::commandPost(switchTo(0)));
}

// A refused post must not have displaced anything: the API turns the refusal
// into a 503 and the client retries, which is only safe if nothing was lost.
void test_a_refused_post_leaves_the_queue_untouched() {
  for (uint8_t i = 0; i < rtos::kCommandQueueDepth; ++i) rtos::commandPost(switchTo(i));
  rtos::commandPost(switchTo(99));

  for (uint8_t i = 0; i < rtos::kCommandQueueDepth; ++i) {
    Command out{};
    TEST_ASSERT_TRUE(rtos::commandTake(out));
    TEST_ASSERT_EQUAL_UINT8(i, out.appIndex);
  }
}

void test_free_tracks_occupancy() {
  TEST_ASSERT_EQUAL_UINT8(rtos::kCommandQueueDepth, rtos::commandFree());

  rtos::commandPost(switchTo(0));
  rtos::commandPost(switchTo(1));
  TEST_ASSERT_EQUAL_UINT8(rtos::kCommandQueueDepth - 2, rtos::commandFree());

  Command out{};
  rtos::commandTake(out);
  TEST_ASSERT_EQUAL_UINT8(rtos::kCommandQueueDepth - 1, rtos::commandFree());
}

// Space is reusable, not consumed once: a ring that only counted up would work
// for a few requests and then wedge.
void test_space_is_reclaimed_by_draining() {
  for (int round = 0; round < 4; ++round) {
    for (uint8_t i = 0; i < rtos::kCommandQueueDepth; ++i) {
      TEST_ASSERT_TRUE(rtos::commandPost(switchTo(i)));
    }
    for (uint8_t i = 0; i < rtos::kCommandQueueDepth; ++i) {
      Command out{};
      TEST_ASSERT_TRUE(rtos::commandTake(out));
      TEST_ASSERT_EQUAL_UINT8(i, out.appIndex);
    }
  }
}

// What handleSetAppSettings relies on to be all-or-nothing: check the room
// first, so a request either lands in full or is refused in full.
void test_free_lets_a_caller_reserve_before_posting() {
  for (uint8_t i = 0; i < rtos::kCommandQueueDepth - 2; ++i) rtos::commandPost(switchTo(i));

  TEST_ASSERT_EQUAL_UINT8(2, rtos::commandFree());
  TEST_ASSERT_TRUE(rtos::commandFree() >= 2);
  TEST_ASSERT_FALSE(rtos::commandFree() >= 3);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_queue_yields_nothing);
  RUN_TEST(test_a_posted_command_comes_back_intact);
  RUN_TEST(test_commands_come_back_in_order);
  RUN_TEST(test_the_command_is_copied_not_referenced);
  RUN_TEST(test_queue_fills_and_then_refuses);
  RUN_TEST(test_a_refused_post_leaves_the_queue_untouched);
  RUN_TEST(test_free_tracks_occupancy);
  RUN_TEST(test_space_is_reclaimed_by_draining);
  RUN_TEST(test_free_lets_a_caller_reserve_before_posting);
  return UNITY_END();
}
