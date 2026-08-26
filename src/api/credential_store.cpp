#include "credential_store.h"

#include <Arduino.h>

#include <cstring>

#include "board/flash_block.h"
#include "hex.h"

extern "C" {
// End of the image the uploader writes. Used to prove the storage block sits
// beyond it. The name is the linker's, so it cannot be renamed to satisfy the
// reserved-identifier check.
extern char __etext;  // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
}

namespace {

constexpr uint32_t kMagic = 0x4d345753;  // "M4WS"
constexpr uint16_t kVersion = 1;

struct StoredBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  StoredClient clients[CredentialStore::kMaxClients];
  uint32_t crc;
};

static_assert(sizeof(StoredBlob) <= flash_block::kBlockSize, "blob exceeds one erase block");
static_assert(sizeof(StoredBlob) % 4 == 0, "flash writes happen in 32-bit words");

uint32_t crc32(const void *data, size_t len) {
  const auto *p = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; ++bit) {
      // Branchless: turns the low bit into an all-ones or all-zeros mask.
      const uint32_t mask = ~((crc & 1U) - 1U);
      crc = (crc >> 1) ^ (0xedb88320U & mask);  // NOLINT(hicpp-signed-bitwise)
    }
  }
  return ~crc;
}

// Everything up to but excluding the trailing crc field.
size_t checksummedLength() { return offsetof(StoredBlob, crc); }

}  // namespace

void CredentialStore::begin() {
  // If the program ever grows into the storage block, an upload would start
  // overwriting credentials again. Cheap to check, and silent corruption is
  // otherwise very hard to attribute.
  const auto imageEnd = reinterpret_cast<uint32_t>(&__etext);
  if (imageEnd >= flash_block::kAddress) {
    Serial.print(F("[creds] FATAL: image ends at 0x"));
    Serial.print(imageEnd, HEX);
    Serial.print(F(" which overlaps the storage block at 0x"));
    Serial.println(flash_block::kAddress, HEX);
  }
  if (!flash_block::geometryMatches()) {
    Serial.println(F("[creds] FATAL: unexpected flash page geometry"));
  }

  load();
}

void CredentialStore::load() {
  StoredBlob blob{};
  flash_block::read(&blob, sizeof(blob));

  count_ = 0;
  memset(clients_, 0, sizeof(clients_));

  // Erased flash reads as 0xFF, so the magic and CRC are what distinguish real
  // data from a blank or half-written block.
  if (blob.magic != kMagic || blob.version != kVersion) {
    Serial.println(F("[creds] no stored credentials"));
    return;
  }
  if (blob.crc != crc32(&blob, checksummedLength())) {
    Serial.println(F("[creds] stored credentials failed checksum, ignoring"));
    return;
  }
  if (blob.count > kMaxClients) {
    Serial.println(F("[creds] stored client count out of range, ignoring"));
    return;
  }

  count_ = static_cast<uint8_t>(blob.count);
  memcpy(clients_, blob.clients, sizeof(clients_));

  Serial.print(F("[creds] loaded "));
  Serial.print(count_);
  Serial.println(F(" client(s)"));
}

void CredentialStore::save() const {
  StoredBlob blob{};
  blob.magic = kMagic;
  blob.version = kVersion;
  blob.count = count_;
  memcpy(blob.clients, clients_, sizeof(clients_));
  blob.crc = crc32(&blob, checksummedLength());

  if (!flash_block::erasedWrite(&blob, sizeof(blob))) {
    Serial.println(F("[creds] flash write did not verify; credentials may not persist"));
  }
}

const StoredClient *CredentialStore::find(const uint8_t id[apiauth::kClientIdBytes]) const {
  for (uint8_t i = 0; i < count_; ++i) {
    if (apiauth::constantTimeEquals(clients_[i].id, id, apiauth::kClientIdBytes)) {
      return &clients_[i];
    }
  }
  return nullptr;
}

bool CredentialStore::add(const StoredClient &client) {
  if (full()) return false;

  clients_[count_] = client;
  ++count_;
  save();
  return true;
}

bool CredentialStore::remove(const uint8_t id[apiauth::kClientIdBytes]) {
  for (uint8_t i = 0; i < count_; ++i) {
    if (!apiauth::constantTimeEquals(clients_[i].id, id, apiauth::kClientIdBytes)) continue;

    for (uint8_t j = i; j + 1 < count_; ++j) {
      clients_[j] = clients_[j + 1];
    }
    --count_;
    memset(&clients_[count_], 0, sizeof(clients_[count_]));
    save();
    return true;
  }
  return false;
}

bool CredentialStore::clear() {
  if (count_ == 0) return false;

  count_ = 0;
  memset(clients_, 0, sizeof(clients_));
  save();
  return true;
}
