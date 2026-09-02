// SettingsBag: discovery, reading, validation, writing, notification.
//
// These limits used to be stated three times -- in the descriptor, again in
// the handler's range check, and a third time inside each app's setSetting()
// -- and nothing made them agree. Now they are stated once, in the binding,
// and this suite is what pins them.

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "apps/settings_bag.h"

namespace {

// Stands in for an app: owns the storage, records the notifications.
struct FakeApp : SettingsOwner {
  bool flag = false;
  int32_t level = 5;
  int32_t colour = 0x00b4ff;
  char label[16] = "hello";

  std::vector<std::string> changed;

  void onSettingChanged(const char *key) override { changed.emplace_back(key); }

  SettingsBag::Binding bindings[4]{
      SettingsBag::boolean("flag", "A flag", flag),
      SettingsBag::integer("level", "A level", 1, 10, level),
      SettingsBag::color("colour", "A colour", colour),
      SettingsBag::text("label", "A label", label),
  };
  SettingsBag bag{*this, bindings, 4};
};

SettingValue boolValue(bool v) {
  SettingValue value{};
  value.type = SettingType::kBool;
  value.boolValue = v;
  return value;
}

SettingValue intValue(int32_t v, SettingType type = SettingType::kInt) {
  SettingValue value{};
  value.type = type;
  value.intValue = v;
  return value;
}

SettingValue stringValue(const char *v) {
  SettingValue value{};
  value.type = SettingType::kString;
  strncpy(value.stringValue, v, sizeof(value.stringValue) - 1);
  return value;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- discovery -------------------------------------------------------------

void test_exposes_descriptors_in_order() {
  FakeApp app;
  TEST_ASSERT_EQUAL_UINT8(4, app.bag.count());
  TEST_ASSERT_EQUAL_STRING("flag", app.bag.descriptor(0).key);
  TEST_ASSERT_EQUAL_STRING("level", app.bag.descriptor(1).key);
  TEST_ASSERT_EQUAL_STRING("colour", app.bag.descriptor(2).key);
  TEST_ASSERT_EQUAL_STRING("label", app.bag.descriptor(3).key);
}

void test_descriptors_carry_their_constraints() {
  FakeApp app;
  TEST_ASSERT_EQUAL(SettingType::kInt, app.bag.descriptor(1).type);
  TEST_ASSERT_EQUAL_INT32(1, app.bag.descriptor(1).intMin);
  TEST_ASSERT_EQUAL_INT32(10, app.bag.descriptor(1).intMax);

  // color() is an int with the 24-bit range filled in, tagged so a UI can
  // render a picker.
  TEST_ASSERT_EQUAL(SettingType::kColor, app.bag.descriptor(2).type);
  TEST_ASSERT_EQUAL_INT32(0xFFFFFF, app.bag.descriptor(2).intMax);

  // text() derives maxLen from the buffer, so the two cannot disagree.
  TEST_ASSERT_EQUAL_UINT32(sizeof(FakeApp{}.label) - 1, app.bag.descriptor(3).maxLen);
}

void test_out_of_range_index_yields_an_empty_descriptor() {
  FakeApp app;
  TEST_ASSERT_EQUAL_STRING("", app.bag.descriptor(4).key);
  TEST_ASSERT_EQUAL_STRING("", app.bag.descriptor(200).key);
}

// --- reading ---------------------------------------------------------------

void test_reads_current_values() {
  FakeApp app;
  SettingValue value{};

  TEST_ASSERT_TRUE(app.bag.get("level", value));
  TEST_ASSERT_EQUAL(SettingType::kInt, value.type);
  TEST_ASSERT_EQUAL_INT32(5, value.intValue);

  TEST_ASSERT_TRUE(app.bag.get("label", value));
  TEST_ASSERT_EQUAL(SettingType::kString, value.type);
  TEST_ASSERT_EQUAL_STRING("hello", value.stringValue);

  TEST_ASSERT_TRUE(app.bag.get("flag", value));
  TEST_ASSERT_EQUAL(SettingType::kBool, value.type);
  TEST_ASSERT_FALSE(value.boolValue);
}

void test_unknown_key_is_not_readable() {
  FakeApp app;
  SettingValue value{};
  TEST_ASSERT_FALSE(app.bag.get("nope", value));
  TEST_ASSERT_FALSE(app.bag.get("", value));
  TEST_ASSERT_FALSE(app.bag.get(nullptr, value));
}

// --- validation ------------------------------------------------------------

void test_rejects_unknown_keys() {
  FakeApp app;
  TEST_ASSERT_FALSE(app.bag.validate("nope", intValue(3)));
  TEST_ASSERT_FALSE(app.bag.apply("nope", intValue(3)));
  TEST_ASSERT_EQUAL_UINT32(0, app.changed.size());
}

// A value of the wrong kind is refused rather than coerced -- otherwise a
// bool would silently become an integer 1.
void test_rejects_type_mismatches() {
  FakeApp app;
  TEST_ASSERT_FALSE(app.bag.validate("level", boolValue(true)));
  TEST_ASSERT_FALSE(app.bag.validate("flag", intValue(1)));
  TEST_ASSERT_FALSE(app.bag.validate("label", intValue(1)));
  TEST_ASSERT_FALSE(app.bag.validate("level", stringValue("3")));

  // kInt and kColor are distinct tags even though both hold an int32.
  TEST_ASSERT_FALSE(app.bag.validate("colour", intValue(1, SettingType::kInt)));
  TEST_ASSERT_FALSE(app.bag.validate("level", intValue(1, SettingType::kColor)));
}

void test_enforces_integer_range_inclusively() {
  FakeApp app;
  TEST_ASSERT_TRUE(app.bag.validate("level", intValue(1)));
  TEST_ASSERT_TRUE(app.bag.validate("level", intValue(10)));
  TEST_ASSERT_FALSE(app.bag.validate("level", intValue(0)));
  TEST_ASSERT_FALSE(app.bag.validate("level", intValue(11)));
  TEST_ASSERT_FALSE(app.bag.validate("level", intValue(-1)));
}

void test_enforces_the_colour_range() {
  FakeApp app;
  TEST_ASSERT_TRUE(app.bag.validate("colour", intValue(0, SettingType::kColor)));
  TEST_ASSERT_TRUE(app.bag.validate("colour", intValue(0xFFFFFF, SettingType::kColor)));
  TEST_ASSERT_FALSE(app.bag.validate("colour", intValue(0x1000000, SettingType::kColor)));
  TEST_ASSERT_FALSE(app.bag.validate("colour", intValue(-1, SettingType::kColor)));
}

void test_enforces_string_length() {
  FakeApp app;
  const size_t maxLen = sizeof(app.label) - 1;

  TEST_ASSERT_TRUE(app.bag.validate("label", stringValue("")));
  TEST_ASSERT_TRUE(app.bag.validate("label", stringValue(std::string(maxLen, 'x').c_str())));
  TEST_ASSERT_FALSE(app.bag.validate("label", stringValue(std::string(maxLen + 1, 'x').c_str())));
}

void test_bools_accept_either_value() {
  FakeApp app;
  TEST_ASSERT_TRUE(app.bag.validate("flag", boolValue(true)));
  TEST_ASSERT_TRUE(app.bag.validate("flag", boolValue(false)));
}

// validate() must be a pure question: asking it must never move anything.
void test_validate_does_not_write() {
  FakeApp app;
  app.bag.validate("level", intValue(9));
  app.bag.validate("label", stringValue("changed"));
  TEST_ASSERT_EQUAL_INT32(5, app.level);
  TEST_ASSERT_EQUAL_STRING("hello", app.label);
  TEST_ASSERT_EQUAL_UINT32(0, app.changed.size());
}

// --- applying --------------------------------------------------------------

void test_apply_writes_through_to_storage() {
  FakeApp app;

  TEST_ASSERT_TRUE(app.bag.apply("flag", boolValue(true)));
  TEST_ASSERT_TRUE(app.flag);

  TEST_ASSERT_TRUE(app.bag.apply("level", intValue(7)));
  TEST_ASSERT_EQUAL_INT32(7, app.level);

  TEST_ASSERT_TRUE(app.bag.apply("colour", intValue(0x123456, SettingType::kColor)));
  TEST_ASSERT_EQUAL_INT32(0x123456, app.colour);

  TEST_ASSERT_TRUE(app.bag.apply("label", stringValue("world")));
  TEST_ASSERT_EQUAL_STRING("world", app.label);
}

// A refused write must leave the previous value completely intact.
void test_rejected_apply_changes_nothing() {
  FakeApp app;
  TEST_ASSERT_FALSE(app.bag.apply("level", intValue(99)));
  TEST_ASSERT_EQUAL_INT32(5, app.level);

  TEST_ASSERT_FALSE(app.bag.apply("label", stringValue(std::string(64, 'x').c_str())));
  TEST_ASSERT_EQUAL_STRING("hello", app.label);

  TEST_ASSERT_EQUAL_UINT32(0, app.changed.size());
}

void test_string_at_the_cap_is_written_and_terminated() {
  FakeApp app;
  const std::string longest(sizeof(app.label) - 1, 'z');

  TEST_ASSERT_TRUE(app.bag.apply("label", stringValue(longest.c_str())));
  TEST_ASSERT_EQUAL_STRING(longest.c_str(), app.label);
  TEST_ASSERT_EQUAL_CHAR('\0', app.label[sizeof(app.label) - 1]);
}

void test_empty_string_is_a_valid_value() {
  FakeApp app;
  TEST_ASSERT_TRUE(app.bag.apply("label", stringValue("")));
  TEST_ASSERT_EQUAL_STRING("", app.label);
}

// --- notification ----------------------------------------------------------

void test_notifies_once_per_applied_key() {
  FakeApp app;
  app.bag.apply("level", intValue(7));
  TEST_ASSERT_EQUAL_UINT32(1, app.changed.size());
  TEST_ASSERT_EQUAL_STRING("level", app.changed[0].c_str());

  app.bag.apply("label", stringValue("world"));
  TEST_ASSERT_EQUAL_UINT32(2, app.changed.size());
  TEST_ASSERT_EQUAL_STRING("label", app.changed[1].c_str());
}

// Writing the same value again still notifies: an app may have invalidated
// its own cached state for other reasons, and suppressing here would make the
// hook's contract conditional and hard to reason about.
void test_notifies_even_when_the_value_is_unchanged() {
  FakeApp app;
  app.bag.apply("level", intValue(5));
  TEST_ASSERT_EQUAL_UINT32(1, app.changed.size());
}

void test_does_not_notify_on_rejection() {
  FakeApp app;
  app.bag.apply("level", intValue(99));
  app.bag.apply("nope", intValue(1));
  app.bag.apply("flag", intValue(1));
  TEST_ASSERT_EQUAL_UINT32(0, app.changed.size());
}

// The key passed to the hook is the binding's own pointer, so an app can
// compare it and rely on it outliving the call.
void test_notification_key_matches_the_descriptor() {
  FakeApp app;
  app.bag.apply("colour", intValue(1, SettingType::kColor));
  TEST_ASSERT_EQUAL_STRING(app.bag.descriptor(2).key, app.changed[0].c_str());
}

// --- round trip ------------------------------------------------------------

// What get() reports must be exactly what apply() accepted; the settings store
// relies on this to persist and restore values faithfully.
void test_written_values_round_trip_through_get() {
  FakeApp app;
  app.bag.apply("flag", boolValue(true));
  app.bag.apply("level", intValue(9));
  app.bag.apply("colour", intValue(0xABCDEF, SettingType::kColor));
  app.bag.apply("label", stringValue("round trip"));

  SettingValue value{};
  TEST_ASSERT_TRUE(app.bag.get("flag", value));
  TEST_ASSERT_TRUE(value.boolValue);
  TEST_ASSERT_TRUE(app.bag.get("level", value));
  TEST_ASSERT_EQUAL_INT32(9, value.intValue);
  TEST_ASSERT_TRUE(app.bag.get("colour", value));
  TEST_ASSERT_EQUAL_INT32(0xABCDEF, value.intValue);
  TEST_ASSERT_TRUE(app.bag.get("label", value));
  TEST_ASSERT_EQUAL_STRING("round trip", value.stringValue);

  // And everything read back validates, so a save/restore cycle cannot
  // produce a value the bag would then refuse.
  for (uint8_t i = 0; i < app.bag.count(); ++i) {
    SettingValue current{};
    const char *key = app.bag.descriptor(i).key;
    TEST_ASSERT_TRUE(app.bag.get(key, current));
    TEST_ASSERT_TRUE(app.bag.validate(key, current));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_exposes_descriptors_in_order);
  RUN_TEST(test_descriptors_carry_their_constraints);
  RUN_TEST(test_out_of_range_index_yields_an_empty_descriptor);
  RUN_TEST(test_reads_current_values);
  RUN_TEST(test_unknown_key_is_not_readable);
  RUN_TEST(test_rejects_unknown_keys);
  RUN_TEST(test_rejects_type_mismatches);
  RUN_TEST(test_enforces_integer_range_inclusively);
  RUN_TEST(test_enforces_the_colour_range);
  RUN_TEST(test_enforces_string_length);
  RUN_TEST(test_bools_accept_either_value);
  RUN_TEST(test_validate_does_not_write);
  RUN_TEST(test_apply_writes_through_to_storage);
  RUN_TEST(test_rejected_apply_changes_nothing);
  RUN_TEST(test_string_at_the_cap_is_written_and_terminated);
  RUN_TEST(test_empty_string_is_a_valid_value);
  RUN_TEST(test_notifies_once_per_applied_key);
  RUN_TEST(test_notifies_even_when_the_value_is_unchanged);
  RUN_TEST(test_does_not_notify_on_rejection);
  RUN_TEST(test_notification_key_matches_the_descriptor);
  RUN_TEST(test_written_values_round_trip_through_get);
  return UNITY_END();
}
