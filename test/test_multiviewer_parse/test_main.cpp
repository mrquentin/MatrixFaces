// MultiViewer feed parsing.
//
// FIXTURE PROVENANCE -- read before trusting these.
//
// No live MultiViewer session was available when this suite was written, so
// the payloads below are *synthesized* from what the parser already accepted
// and from the community-documented shape of the F1 SignalR topics. They are
// therefore a regression net for refactoring -- they will catch the parser
// changing behaviour -- but they are NOT evidence that the parser matches what
// MultiViewer actually emits.
//
// TODO-verify: capture a real response during a session (tools/mv_mock.py
// --record) and replace these, keeping any case that then disagrees.

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "net/multiviewer_parse.h"

namespace {

// parse() mutates its buffer, so every call gets a fresh copy.
mv::ParseResult run(const std::string &json, uint32_t nowMs, mv::SessionState &state,
                    mv::Counters &counters) {
  std::vector<char> buffer(json.begin(), json.end());
  buffer.push_back('\0');
  return mv::parse(buffer.data(), json.size(), nowMs, state, counters);
}

// Wraps topic JSON in the envelope the GraphQL endpoint returns.
std::string envelope(const std::string &topics) {
  return R"({"data":{"f1LiveTimingState":{)" + topics + R"(}}})";
}

std::string trackStatus(const char *message) {
  return R"("TrackStatus":{"Status":"1","Message":")" + std::string(message) + R"("})";
}

// A driver-scoped race control message.
std::string flagMessage(int index, const char *flag, const std::string &racingNumber,
                        const char *scope = "Driver", const char *category = "Flag") {
  return "\"" + std::to_string(index) + R"(":{"Utc":"2026-09-01T14:00:00","Category":")" +
         category + R"(","Flag":")" + flag + R"(","Scope":")" + scope +
         R"(","RacingNumber":)" + racingNumber + R"(,"Message":"WAVED BLUE FLAG"})";
}

std::string raceControl(const std::vector<std::string> &messages) {
  std::string joined;
  for (size_t i = 0; i < messages.size(); ++i) {
    if (i != 0) joined += ",";
    joined += messages[i];
  }
  return R"("RaceControlMessages":{"Messages":{)" + joined + R"(}})";
}

const char *kDriverList =
    R"("DriverList":{"1":{"RacingNumber":"1","Tla":"VER","FullName":"Max Verstappen"},)"
    R"("44":{"RacingNumber":"44","Tla":"HAM","FullName":"Lewis Hamilton"},)"
    R"("16":{"RacingNumber":"16","Tla":"LEC","FullName":"Charles Leclerc"}})";

const char *kLapCount = R"("LapCount":{"CurrentLap":12,"TotalLaps":57})";

mv::SessionState g_state;
mv::Counters g_counters;

}  // namespace

void setUp() {
  g_state = mv::SessionState{};
  g_counters = mv::Counters{};
}
void tearDown() {}

// --- envelope handling -----------------------------------------------------

void test_parses_a_full_response() {
  const std::string json = envelope(trackStatus("AllClear") + "," + kLapCount + "," +
                                    kDriverList + "," + raceControl({}));
  TEST_ASSERT_EQUAL(mv::ParseResult::kOk, run(json, 1000, g_state, g_counters));

  TEST_ASSERT_EQUAL(mv::Flag::kAllClear, g_state.trackFlag);
  TEST_ASSERT_TRUE(g_state.hasLapCount);
  TEST_ASSERT_EQUAL_UINT32(12, g_state.currentLap);
  TEST_ASSERT_EQUAL_UINT32(57, g_state.totalLaps);
  TEST_ASSERT_EQUAL_STRING("VER", g_state.tlaForRacingNumber(1));
  TEST_ASSERT_EQUAL_STRING("HAM", g_state.tlaForRacingNumber(44));
  TEST_ASSERT_EQUAL_UINT32(1, g_counters.parsed);
}

// MultiViewer is up but has no session open. This is a real state, not an
// error, and it must clear stale data rather than leaving last race's flag up.
void test_null_state_resets_the_session() {
  g_state.trackFlag = mv::Flag::kRed;
  g_state.hasLapCount = true;
  g_state.currentLap = 40;

  const std::string json = R"({"data":{"f1LiveTimingState":null}})";
  TEST_ASSERT_EQUAL(mv::ParseResult::kNoSession, run(json, 1000, g_state, g_counters));

  TEST_ASSERT_EQUAL(mv::Flag::kUnknown, g_state.trackFlag);
  TEST_ASSERT_FALSE(g_state.hasLapCount);
  TEST_ASSERT_EQUAL_UINT32(1, g_counters.noSession);
}

// A garbled poll must not blank the display -- prior state survives, and the
// counter is what makes the failure visible.
void test_malformed_response_preserves_prior_state() {
  g_state.trackFlag = mv::Flag::kSafetyCar;

  TEST_ASSERT_EQUAL(mv::ParseResult::kMalformed,
                    run(R"({"errors":[{"message":"boom"}]})", 1000, g_state, g_counters));
  TEST_ASSERT_EQUAL(mv::Flag::kSafetyCar, g_state.trackFlag);
  TEST_ASSERT_EQUAL_UINT32(1, g_counters.malformed);

  // Present but not an object is malformed too.
  TEST_ASSERT_EQUAL(mv::ParseResult::kMalformed,
                    run(R"({"data":{"f1LiveTimingState":"nope"}})", 1000, g_state, g_counters));
  TEST_ASSERT_EQUAL_UINT32(2, g_counters.malformed);
}

// The response ran past the 32 KB buffer. Distinguished from malformed because
// it means "raise the cap or trim the query", not "the schema changed".
void test_truncated_response_is_reported_separately() {
  std::string json = envelope(trackStatus("Yellow") + "," + kDriverList);
  json.resize(json.size() - 12);  // lop off the closing braces

  TEST_ASSERT_EQUAL(mv::ParseResult::kTruncated, run(json, 1000, g_state, g_counters));
  TEST_ASSERT_EQUAL_UINT32(1, g_counters.truncated);
  TEST_ASSERT_EQUAL_UINT32(0, g_counters.malformed);
}

// --- TrackStatus -----------------------------------------------------------

void test_maps_every_known_track_status() {
  struct Case {
    const char *message;
    mv::Flag expected;
  };
  const Case cases[] = {
      {"AllClear", mv::Flag::kAllClear},
      {"Yellow", mv::Flag::kYellow},
      {"DoubleYellow", mv::Flag::kYellow},
      {"SCDeployed", mv::Flag::kSafetyCar},
      {"VSCDeployed", mv::Flag::kVirtualSafetyCar},
      {"VSCEnding", mv::Flag::kVirtualSafetyCar},
      {"Red", mv::Flag::kRed},
  };

  for (const Case &c : cases) {
    mv::SessionState state;
    mv::Counters counters;
    run(envelope(trackStatus(c.message)), 1000, state, counters);
    TEST_ASSERT_EQUAL(c.expected, state.trackFlag);
  }
}

// An unrecognised status degrades to "unknown" rather than being guessed at,
// so a MultiViewer-side addition blanks the flag instead of showing a wrong one.
void test_unknown_track_status_degrades() {
  run(envelope(trackStatus("SomethingNew")), 1000, g_state, g_counters);
  TEST_ASSERT_EQUAL(mv::Flag::kUnknown, g_state.trackFlag);
}

void test_missing_track_status_degrades() {
  run(envelope(std::string(kLapCount)), 1000, g_state, g_counters);
  TEST_ASSERT_EQUAL(mv::Flag::kUnknown, g_state.trackFlag);
}

// --- LapCount --------------------------------------------------------------

// The feed sends partial LapCount updates; whichever field is absent keeps its
// previous value rather than resetting to zero mid-race.
void test_partial_lap_count_keeps_the_other_field() {
  run(envelope(std::string(kLapCount)), 1000, g_state, g_counters);
  TEST_ASSERT_EQUAL_UINT32(12, g_state.currentLap);
  TEST_ASSERT_EQUAL_UINT32(57, g_state.totalLaps);

  run(envelope(R"("LapCount":{"CurrentLap":13})"), 2000, g_state, g_counters);
  TEST_ASSERT_TRUE(g_state.hasLapCount);
  TEST_ASSERT_EQUAL_UINT32(13, g_state.currentLap);
  TEST_ASSERT_EQUAL_UINT32(57, g_state.totalLaps);

  run(envelope(R"("LapCount":{"TotalLaps":58})"), 3000, g_state, g_counters);
  TEST_ASSERT_EQUAL_UINT32(13, g_state.currentLap);
  TEST_ASSERT_EQUAL_UINT32(58, g_state.totalLaps);
}

void test_lap_count_absent_clears_the_flag() {
  run(envelope(std::string(kLapCount)), 1000, g_state, g_counters);
  TEST_ASSERT_TRUE(g_state.hasLapCount);

  run(envelope(trackStatus("AllClear")), 2000, g_state, g_counters);
  TEST_ASSERT_FALSE(g_state.hasLapCount);
}

// --- blue flags ------------------------------------------------------------

void test_blue_flag_resolves_to_a_tla() {
  const std::string json = envelope(
      std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "44")}));
  run(json, 10000, g_state, g_counters);

  TEST_ASSERT_TRUE(g_state.hasBlueFlag());
  TEST_ASSERT_EQUAL_STRING("HAM", g_state.blueFlagTla);
}

// RacingNumber comes through as a bare number in some messages and a quoted
// string in others; both have to work.
void test_blue_flag_accepts_a_quoted_racing_number() {
  const std::string json = envelope(
      std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "\"16\"")}));
  run(json, 10000, g_state, g_counters);

  TEST_ASSERT_TRUE(g_state.hasBlueFlag());
  TEST_ASSERT_EQUAL_STRING("LEC", g_state.blueFlagTla);
}

// An explicit CLEAR rescinds immediately rather than waiting out the timeout.
void test_clear_message_rescinds_a_blue_flag() {
  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "44")})),
      10000, g_state, g_counters);
  TEST_ASSERT_TRUE(g_state.hasBlueFlag());

  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(2, "CLEAR", "44")})),
      11000, g_state, g_counters);
  TEST_ASSERT_FALSE(g_state.hasBlueFlag());
}

// The timeout is the safety net for a CLEAR that never arrived.
void test_blue_flag_ages_out() {
  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "44")})),
      10000, g_state, g_counters);
  TEST_ASSERT_TRUE(g_state.hasBlueFlag());

  // Same messages replayed, but far enough later that the flag has expired.
  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "44")})),
      10000 + mv::kBlueFlagTimeoutMs + 1, g_state, g_counters);
  TEST_ASSERT_FALSE(g_state.hasBlueFlag());
}

// Non-Flag categories and track-wide scopes are not driver blue flags.
void test_ignores_non_driver_flag_messages() {
  run(envelope(std::string(kDriverList) + "," +
               raceControl({flagMessage(1, "BLUE", "44", "Track"),
                            flagMessage(2, "BLUE", "1", "Driver", "Other")})),
      10000, g_state, g_counters);
  TEST_ASSERT_FALSE(g_state.hasBlueFlag());
}

// A driver with no DriverList entry has no TLA to show, so nothing is
// displayed rather than a blank or a racing number.
void test_blue_flag_for_unknown_driver_shows_nothing() {
  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "99")})),
      10000, g_state, g_counters);
  TEST_ASSERT_FALSE(g_state.hasBlueFlag());
}

// --- message indices -------------------------------------------------------

// Messages already seen are skipped, so re-polling the same growing list does
// not re-raise a flag that was cleared.
void test_already_seen_messages_are_skipped() {
  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "44")})),
      10000, g_state, g_counters);
  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "44"),
                                                             flagMessage(2, "CLEAR", "44")})),
      11000, g_state, g_counters);
  TEST_ASSERT_FALSE(g_state.hasBlueFlag());

  // Replaying the whole list must not resurrect message 1.
  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(1, "BLUE", "44"),
                                                             flagMessage(2, "CLEAR", "44")})),
      12000, g_state, g_counters);
  TEST_ASSERT_FALSE(g_state.hasBlueFlag());
}

// Indices restarting from a lower value means a new session began; carrying
// the old high-water mark forward would ignore the entire new session.
void test_restarted_indices_reset_tracking() {
  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(40, "BLUE", "44")})),
      10000, g_state, g_counters);
  TEST_ASSERT_EQUAL_INT32(40, g_state.lastMessageIndex);

  run(envelope(std::string(kDriverList) + "," + raceControl({flagMessage(1, "CLEAR", "44")})),
      11000, g_state, g_counters);
  TEST_ASSERT_EQUAL_INT32(-1, g_state.lastMessageIndex);
  TEST_ASSERT_FALSE(g_state.hasBlueFlag());
}

// --- scanner robustness ----------------------------------------------------

// The object scanner tracks string state, so a brace or quote inside a message
// body must not be mistaken for structure.
void test_braces_and_quotes_inside_strings_do_not_confuse_the_scanner() {
  const std::string tricky =
      R"("RaceControlMessages":{"Messages":{"1":{"Category":"Other","Message":"pit } entry \" open {"}}})";
  const std::string json = envelope(trackStatus("Yellow") + "," + tricky + "," + kLapCount);

  TEST_ASSERT_EQUAL(mv::ParseResult::kOk, run(json, 1000, g_state, g_counters));
  TEST_ASSERT_EQUAL(mv::Flag::kYellow, g_state.trackFlag);
  TEST_ASSERT_TRUE(g_state.hasLapCount);
  TEST_ASSERT_EQUAL_UINT32(12, g_state.currentLap);
}

// More drivers than the fixed table holds: the scan stops rather than
// overrunning, and the entries it did take are intact.
void test_driver_list_is_bounded() {
  std::string entries;
  for (int i = 1; i <= 40; ++i) {
    if (i != 1) entries += ",";
    entries += "\"" + std::to_string(i) + R"(":{"Tla":"D)" + std::to_string(i % 10) + R"("})";
  }
  run(envelope(R"("DriverList":{)" + entries + "}"), 1000, g_state, g_counters);

  TEST_ASSERT_EQUAL_STRING("D1", g_state.tlaForRacingNumber(1));
  TEST_ASSERT_NOT_NULL(g_state.tlaForRacingNumber(mv::kMaxDrivers));
  TEST_ASSERT_NULL(g_state.tlaForRacingNumber(40));
}

void test_counters_accumulate_across_polls() {
  run(envelope(trackStatus("AllClear")), 1000, g_state, g_counters);
  run(R"({"data":{"f1LiveTimingState":null}})", 2000, g_state, g_counters);
  run(R"({"nope":1})", 3000, g_state, g_counters);
  run(envelope(trackStatus("Red")), 4000, g_state, g_counters);

  TEST_ASSERT_EQUAL_UINT32(2, g_counters.parsed);
  TEST_ASSERT_EQUAL_UINT32(1, g_counters.noSession);
  TEST_ASSERT_EQUAL_UINT32(1, g_counters.malformed);
  TEST_ASSERT_EQUAL_UINT32(0, g_counters.truncated);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_full_response);
  RUN_TEST(test_null_state_resets_the_session);
  RUN_TEST(test_malformed_response_preserves_prior_state);
  RUN_TEST(test_truncated_response_is_reported_separately);
  RUN_TEST(test_maps_every_known_track_status);
  RUN_TEST(test_unknown_track_status_degrades);
  RUN_TEST(test_missing_track_status_degrades);
  RUN_TEST(test_partial_lap_count_keeps_the_other_field);
  RUN_TEST(test_lap_count_absent_clears_the_flag);
  RUN_TEST(test_blue_flag_resolves_to_a_tla);
  RUN_TEST(test_blue_flag_accepts_a_quoted_racing_number);
  RUN_TEST(test_clear_message_rescinds_a_blue_flag);
  RUN_TEST(test_blue_flag_ages_out);
  RUN_TEST(test_ignores_non_driver_flag_messages);
  RUN_TEST(test_blue_flag_for_unknown_driver_shows_nothing);
  RUN_TEST(test_already_seen_messages_are_skipped);
  RUN_TEST(test_restarted_indices_reset_tracking);
  RUN_TEST(test_braces_and_quotes_inside_strings_do_not_confuse_the_scanner);
  RUN_TEST(test_driver_list_is_bounded);
  RUN_TEST(test_counters_accumulate_across_polls);
  return UNITY_END();
}
