#pragma once

#include <cstddef>
#include <cstdint>

// A setting value's kind. Kept to a small closed set so the wire format and
// every app's storage stay simple; add a case here (and in the API layer)
// if a new kind is ever needed.
enum class SettingType : uint8_t {
  kBool,
  kInt,
  kString,
};

// Static description of one setting, returned by App::settingDescriptor() so
// a generic controller can discover -- and render a form for -- every app's
// configuration without needing per-app knowledge baked in.
//
// Plain aggregate, no default member initializers: always construct with all
// fields (unused ones as 0), matching StoredClient/HttpRequest elsewhere in
// this codebase, since brace-initializing a partial aggregate that also has
// in-class initializers is only portable from C++17 on.
struct SettingDescriptor {
  const char *key;    // stable identifier used in getSetting()/setSetting()
  const char *label;  // human-readable, for a UI to display
  SettingType type;

  // Meaning depends on `type`; ignored otherwise.
  int32_t intMin;
  int32_t intMax;
  size_t maxLen;  // kString: max characters, excluding the terminator
};

// A setting's value, tagged by `type`. All fields are always present (fixed
// size, no heap) but only the one matching `type` is meaningful.
struct SettingValue {
  static constexpr size_t kStringCap = 32;

  SettingType type;
  bool boolValue;
  int32_t intValue;
  char stringValue[kStringCap];
};
