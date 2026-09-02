#pragma once

#include <cstddef>
#include <cstdint>

#include "app_setting.h"

// Notified after one of its settings changes. App implements this; it exists
// as its own interface so SettingsBag needs nothing from the app or matrix
// layers and can be built and tested on the host by itself.
class SettingsOwner {
 public:
  virtual ~SettingsOwner() = default;

  // Called once per applied key. This is where an app reacts -- invalidating
  // cached layout, forcing a redraw, reconnecting -- and is the only part of
  // the settings mechanism that stays app-specific.
  virtual void onSettingChanged(const char *key) { (void)key; }
};

// Binds an app's settings to the members that hold them, so that discovery,
// reading, validation and writing are all generic.
//
// This replaces a per-app get/setSetting pair -- a strcmp chain that restated
// each setting's type and range a second time, and its storage a third. Those
// restatements were the bug surface: the descriptor, the validation and the
// assignment could disagree, and nothing would say so.
//
// What an app still writes for itself is the *reaction* to a change
// (App::onSettingChanged), which is the only genuinely app-specific part.
class SettingsBag {
 public:
  struct Binding {
    SettingDescriptor descriptor;
    // Exactly one of these is live, chosen by descriptor.type. The factories
    // below are the only way to build a Binding, which is what keeps the tag
    // and the pointer in agreement.
    union Storage {
      bool *boolean;
      int32_t *integer;
      char *text;
    } storage;
  };

  static Binding boolean(const char *key, const char *label, bool &storage) {
    Binding binding{{key, label, SettingType::kBool, 0, 0, 0}, {}};
    binding.storage.boolean = &storage;
    return binding;
  }

  static Binding integer(const char *key, const char *label, int32_t min, int32_t max,
                         int32_t &storage) {
    Binding binding{{key, label, SettingType::kInt, min, max, 0}, {}};
    binding.storage.integer = &storage;
    return binding;
  }

  // Packed 0xRRGGBB. Identical to integer() apart from the tag, which exists
  // so a generic UI can offer a colour picker without special-casing keys.
  static Binding color(const char *key, const char *label, int32_t &storage) {
    Binding binding{{key, label, SettingType::kColor, 0, 0xFFFFFF, 0}, {}};
    binding.storage.integer = &storage;
    return binding;
  }

  // maxLen comes from the buffer itself, so the declared limit and the space
  // actually available cannot drift apart.
  template <size_t N>
  static Binding text(const char *key, const char *label, char (&storage)[N]) {
    static_assert(N >= 2, "a text setting needs room for at least one character");
    static_assert(N <= SettingValue::kStringCap,
                  "storage exceeds what a SettingValue can carry over the API");
    Binding binding{{key, label, SettingType::kString, 0, 0, N - 1}, {}};
    binding.storage.text = storage;
    return binding;
  }

  // `bindings` must outlive the bag; in practice it is a member array of the
  // same app.
  SettingsBag(SettingsOwner &owner, Binding *bindings, uint8_t count)
      : owner_(owner), bindings_(bindings), count_(count) {}

  uint8_t count() const { return count_; }

  // Returned for an index past count(), and by anything that has no bag at
  // all, so a caller that ignores count() still gets a well-formed object.
  // One definition, because five copies of this is where the old code started.
  static const SettingDescriptor &emptyDescriptor();

  const SettingDescriptor &descriptor(uint8_t index) const;

  // Reads the current value. False if `key` is unknown.
  bool get(const char *key, SettingValue &out) const;

  // True if `value` could be written to `key`: known key, matching type, and
  // within the descriptor's range or length. Touches nothing.
  //
  // Split from apply() so a request carrying several settings can be checked
  // in full before any of it lands -- and, from phase 4, so validation can run
  // on the network task while the write happens on the render task.
  bool validate(const char *key, const SettingValue &value) const;

  // Writes the value and notifies the owner exactly once. Returns false
  // without writing if validate() would have refused.
  bool apply(const char *key, const SettingValue &value);

 private:
  const Binding *find(const char *key) const;

  SettingsOwner &owner_;
  Binding *bindings_;
  uint8_t count_;
};
