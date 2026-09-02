#include "board/blob_store.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "board/native/blob_store_native.h"

// In-memory persistence for host builds. Reproduces the two behaviours that
// matter to callers: named slots are independent, and a slot that has never
// been written reads as erased flash does, all 0xFF -- which is what makes
// "blank" testable without a board.
namespace {

std::map<std::string, std::vector<uint8_t>> g_slots;

bool known(const char *name) {
  return name != nullptr && (strcmp(name, blob_store::kCredentials) == 0 ||
                             strcmp(name, blob_store::kAppSettings) == 0);
}

}  // namespace

namespace blob_store_native {

void reset() { g_slots.clear(); }

bool wasWritten(const char *name) { return g_slots.count(name) != 0; }

}  // namespace blob_store_native

namespace blob_store {

bool checkPlacement(const char *name) { return known(name); }

bool load(const char *name, void *buf, size_t cap, size_t &outLen) {
  if (!known(name)) return false;

  auto it = g_slots.find(name);
  if (it == g_slots.end()) {
    memset(buf, 0xFF, cap);
    outLen = cap;
    return true;
  }

  const size_t n = it->second.size() < cap ? it->second.size() : cap;
  memcpy(buf, it->second.data(), n);
  outLen = n;
  return true;
}

bool save(const char *name, const void *buf, size_t len) {
  if (!known(name)) return false;

  const auto *bytes = static_cast<const uint8_t *>(buf);
  g_slots[name].assign(bytes, bytes + len);
  return true;
}

}  // namespace blob_store
