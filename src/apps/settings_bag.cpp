#include "apps/settings_bag.h"

#include <cstring>

const SettingDescriptor &SettingsBag::emptyDescriptor() {
  static constexpr SettingDescriptor kEmpty{"", "", SettingType::kBool, 0, 0, 0};
  return kEmpty;
}

const SettingDescriptor &SettingsBag::descriptor(uint8_t index) const {
  return index < count_ ? bindings_[index].descriptor : emptyDescriptor();
}

const SettingsBag::Binding *SettingsBag::find(const char *key) const {
  if (key == nullptr) return nullptr;
  for (uint8_t i = 0; i < count_; ++i) {
    if (strcmp(bindings_[i].descriptor.key, key) == 0) return &bindings_[i];
  }
  return nullptr;
}

bool SettingsBag::get(const char *key, SettingValue &out) const {
  const Binding *binding = find(key);
  if (binding == nullptr) return false;

  out = SettingValue{};
  out.type = binding->descriptor.type;

  switch (binding->descriptor.type) {
    case SettingType::kBool:
      out.boolValue = *binding->storage.boolean;
      return true;
    case SettingType::kInt:
    case SettingType::kColor:
      out.intValue = *binding->storage.integer;
      return true;
    case SettingType::kString:
      strncpy(out.stringValue, binding->storage.text, sizeof(out.stringValue) - 1);
      out.stringValue[sizeof(out.stringValue) - 1] = '\0';
      return true;
  }
  return false;
}

bool SettingsBag::validate(const char *key, const SettingValue &value) const {
  const Binding *binding = find(key);
  if (binding == nullptr) return false;

  const SettingDescriptor &descriptor = binding->descriptor;
  if (value.type != descriptor.type) return false;

  switch (descriptor.type) {
    case SettingType::kBool:
      return true;
    case SettingType::kInt:
    case SettingType::kColor:
      return value.intValue >= descriptor.intMin && value.intValue <= descriptor.intMax;
    case SettingType::kString:
      return strnlen(value.stringValue, sizeof(value.stringValue)) <= descriptor.maxLen;
  }
  return false;
}

bool SettingsBag::apply(const char *key, const SettingValue &value) {
  if (!validate(key, value)) return false;

  const Binding *binding = find(key);
  switch (binding->descriptor.type) {
    case SettingType::kBool:
      *binding->storage.boolean = value.boolValue;
      break;
    case SettingType::kInt:
    case SettingType::kColor:
      *binding->storage.integer = value.intValue;
      break;
    case SettingType::kString: {
      // validate() already bounded this by maxLen, which text() derived from
      // the buffer's own size.
      const size_t len = strnlen(value.stringValue, sizeof(value.stringValue));
      memcpy(binding->storage.text, value.stringValue, len);
      binding->storage.text[len] = '\0';
      break;
    }
  }

  owner_.onSettingChanged(binding->descriptor.key);
  return true;
}
