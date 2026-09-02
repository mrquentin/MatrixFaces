#include "apps/settings_bag.h"

#include <cstring>

#include "board/rtos.h"

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

namespace {

// One lock for every bag, not one per bag. Reads come from the network task
// and writes from the render task, and it is only ever held for the length of
// a scalar assignment or a 32-byte copy -- so there is nothing to gain from
// finer grain, and a single lock is one fewer thing to reason about.
//
// It exists for the string values. A bool or an int32 is written atomically by
// any machine this runs on, but a char[32] is not: a GET landing mid-write
// would see half of one value and half of another. A no-op on the boards that
// run one task.
rtos::Mutex &settingsMutex() {
  static rtos::Mutex mutex;
  return mutex;
}

}  // namespace

bool SettingsBag::get(const char *key, SettingValue &out) const {
  const Binding *binding = find(key);
  if (binding == nullptr) return false;

  rtos::LockGuard lock(settingsMutex());
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
  // Validated outside the lock: it reads the descriptor, which is const, and
  // never the storage.
  if (!validate(key, value)) return false;

  const Binding *binding = find(key);

  // Scoped to the write alone. onSettingChanged() below is an app's own code
  // and takes locks of its own -- F1FlagsApp's reaches into MvLink -- and
  // holding two at once is how an ordering people have to remember gets
  // created. Nothing takes them the other way round today, and this is how it
  // stays that way by construction rather than by vigilance.
  {
    rtos::LockGuard lock(settingsMutex());
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
  }

  owner_.onSettingChanged(binding->descriptor.key);
  return true;
}
