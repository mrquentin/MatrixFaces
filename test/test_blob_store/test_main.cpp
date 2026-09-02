// The on-storage blob format, and the blob_store contract.
//
// This is the suite that stands between a firmware update and every paired
// client on the device. The format below is what the previous firmware wrote
// via FlashRecordStore; if it changes, a board taking an update finds no
// credentials and silently forgets who it was paired with. So the layout is
// pinned by explicit byte goldens, not just by round-tripping.

#include <unity.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "board/blob_store.h"
#include "storage/record_blob.h"

// ---------------------------------------------------------------------------
// In-memory blob_store, standing in for a board. Exercising the real stores
// against this is what makes the contract testable without hardware.
// ---------------------------------------------------------------------------
namespace {

std::map<std::string, std::vector<uint8_t>> g_blobs;
bool g_placementOk = true;
int g_saveCount = 0;

bool known(const char *name) {
  return name != nullptr && (strcmp(name, blob_store::kCredentials) == 0 ||
                             strcmp(name, blob_store::kAppSettings) == 0);
}

// Mirrors the record the credential store persists.
struct FakeClient {
  uint8_t id[8];
  uint8_t secret[32];
  uint32_t pairedAt;
};
static_assert(sizeof(FakeClient) == 44, "must match StoredClient");

constexpr uint32_t kCredMagic = 0x4d345753;  // "M4WS"
constexpr uint16_t kVersion = 1;
constexpr uint16_t kMaxClients = 4;
constexpr size_t kCredBlobSize = record_blob::framedSize(sizeof(FakeClient), kMaxClients);

}  // namespace

namespace blob_store {

bool checkPlacement(const char *name) { return g_placementOk && known(name); }

bool load(const char *name, void *buf, size_t cap, size_t &outLen) {
  if (!known(name)) return false;
  auto it = g_blobs.find(name);
  if (it == g_blobs.end()) {
    // Erased flash reads as 0xFF; the fake reproduces that so "blank" is
    // exercised the same way it happens on a real board.
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
  ++g_saveCount;
  const auto *bytes = static_cast<const uint8_t *>(buf);
  g_blobs[name].assign(bytes, bytes + len);
  return true;
}

}  // namespace blob_store

void setUp() {
  g_blobs.clear();
  g_placementOk = true;
  g_saveCount = 0;
}
void tearDown() {}

// --- CRC -------------------------------------------------------------------

// The check value for CRC-32/ISO-HDLC. If this drifts, every blob the previous
// firmware wrote fails its checksum and the device forgets everything.
void test_crc32_matches_the_standard_check_value() {
  const char *input = "123456789";
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, record_blob::crc32(input, strlen(input)));
}

void test_crc32_of_empty_input() {
  TEST_ASSERT_EQUAL_HEX32(0x00000000, record_blob::crc32("", 0));
}

// --- byte layout -----------------------------------------------------------

void test_framed_size_is_header_records_and_crc() {
  TEST_ASSERT_EQUAL_UINT32(8 + 44 * 4 + 4, kCredBlobSize);
  TEST_ASSERT_EQUAL_UINT32(188, kCredBlobSize);
  // The settings blob, for the same reason.
  TEST_ASSERT_EQUAL_UINT32(8 + 72 * 32 + 4, record_blob::framedSize(72, 32));
}

// The golden. Header fields are little-endian, records follow at offset 8,
// CRC is the last four bytes and covers everything before it.
void test_serialized_layout_is_exact() {
  FakeClient clients[kMaxClients];
  memset(clients, 0, sizeof(clients));
  for (int i = 0; i < 8; ++i) clients[0].id[i] = static_cast<uint8_t>(0xA0 + i);
  for (int i = 0; i < 32; ++i) clients[0].secret[i] = static_cast<uint8_t>(i);
  clients[0].pairedAt = 0x11223344;

  uint8_t blob[kCredBlobSize];
  TEST_ASSERT_TRUE(record_blob::serialize(blob, sizeof(blob), kCredMagic, kVersion, clients,
                                          sizeof(FakeClient), kMaxClients, 1));

  // magic "M4WS" little-endian
  TEST_ASSERT_EQUAL_HEX8(0x53, blob[0]);
  TEST_ASSERT_EQUAL_HEX8(0x57, blob[1]);
  TEST_ASSERT_EQUAL_HEX8(0x34, blob[2]);
  TEST_ASSERT_EQUAL_HEX8(0x4D, blob[3]);
  // version 1, count 1
  TEST_ASSERT_EQUAL_HEX8(0x01, blob[4]);
  TEST_ASSERT_EQUAL_HEX8(0x00, blob[5]);
  TEST_ASSERT_EQUAL_HEX8(0x01, blob[6]);
  TEST_ASSERT_EQUAL_HEX8(0x00, blob[7]);
  // records begin at offset 8, with no padding after the header
  TEST_ASSERT_EQUAL_HEX8(0xA0, blob[8]);
  TEST_ASSERT_EQUAL_HEX8(0xA7, blob[15]);
  TEST_ASSERT_EQUAL_HEX8(0x00, blob[16]);  // secret[0]
  TEST_ASSERT_EQUAL_HEX8(0x1F, blob[47]);  // secret[31]
  TEST_ASSERT_EQUAL_HEX8(0x44, blob[48]);  // pairedAt, little-endian
  TEST_ASSERT_EQUAL_HEX8(0x11, blob[51]);

  // Unused record slots are zeroed, so identical contents always give
  // identical bytes -- and therefore an identical CRC.
  for (size_t i = 8 + sizeof(FakeClient); i < kCredBlobSize - 4; ++i) {
    TEST_ASSERT_EQUAL_HEX8(0x00, blob[i]);
  }

  // CRC covers everything before itself.
  const uint32_t expected = record_blob::crc32(blob, kCredBlobSize - 4);
  uint32_t stored = 0;
  memcpy(&stored, blob + kCredBlobSize - 4, 4);
  TEST_ASSERT_EQUAL_HEX32(expected, stored);
}

// Same logical contents must always give the same bytes, or a save would
// needlessly cycle the flash and, worse, a golden would be meaningless.
void test_serialization_is_deterministic() {
  FakeClient clients[kMaxClients];
  memset(clients, 0, sizeof(clients));
  clients[0].pairedAt = 42;

  uint8_t first[kCredBlobSize];
  uint8_t second[kCredBlobSize];
  record_blob::serialize(first, sizeof(first), kCredMagic, kVersion, clients, sizeof(FakeClient),
                         kMaxClients, 1);
  record_blob::serialize(second, sizeof(second), kCredMagic, kVersion, clients,
                         sizeof(FakeClient), kMaxClients, 1);
  TEST_ASSERT_EQUAL_MEMORY(first, second, kCredBlobSize);
}

// --- round trip ------------------------------------------------------------

void test_round_trips_records() {
  FakeClient out[kMaxClients];
  memset(out, 0, sizeof(out));
  out[0].pairedAt = 1000;
  out[1].pairedAt = 2000;
  out[1].id[0] = 0xEE;

  uint8_t blob[kCredBlobSize];
  TEST_ASSERT_TRUE(record_blob::serialize(blob, sizeof(blob), kCredMagic, kVersion, out,
                                          sizeof(FakeClient), kMaxClients, 2));

  FakeClient back[kMaxClients];
  memset(back, 0, sizeof(back));
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kOk,
                    record_blob::parse(blob, sizeof(blob), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
  TEST_ASSERT_EQUAL_UINT16(2, count);
  TEST_ASSERT_EQUAL_UINT32(1000, back[0].pairedAt);
  TEST_ASSERT_EQUAL_UINT32(2000, back[1].pairedAt);
  TEST_ASSERT_EQUAL_HEX8(0xEE, back[1].id[0]);
}

void test_round_trips_an_empty_store() {
  FakeClient none[kMaxClients];
  uint8_t blob[kCredBlobSize];
  TEST_ASSERT_TRUE(record_blob::serialize(blob, sizeof(blob), kCredMagic, kVersion, none,
                                          sizeof(FakeClient), kMaxClients, 0));

  FakeClient back[kMaxClients];
  uint16_t count = 99;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kOk,
                    record_blob::parse(blob, sizeof(blob), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
  TEST_ASSERT_EQUAL_UINT16(0, count);
}

void test_round_trips_a_full_store() {
  FakeClient all[kMaxClients];
  memset(all, 0, sizeof(all));
  for (uint16_t i = 0; i < kMaxClients; ++i) all[i].pairedAt = 100u + i;

  uint8_t blob[kCredBlobSize];
  TEST_ASSERT_TRUE(record_blob::serialize(blob, sizeof(blob), kCredMagic, kVersion, all,
                                          sizeof(FakeClient), kMaxClients, kMaxClients));

  FakeClient back[kMaxClients];
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kOk,
                    record_blob::parse(blob, sizeof(blob), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
  TEST_ASSERT_EQUAL_UINT16(kMaxClients, count);
  TEST_ASSERT_EQUAL_UINT32(103, back[3].pairedAt);
}

// --- rejection -------------------------------------------------------------

// Blank flash must read as "nothing stored", never as a corrupt store, or the
// log would cry wolf on every first boot.
void test_blank_flash_reads_as_no_data() {
  uint8_t blank[kCredBlobSize];
  memset(blank, 0xFF, sizeof(blank));

  FakeClient back[kMaxClients];
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kNoData,
                    record_blob::parse(blank, sizeof(blank), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
}

void test_rejects_another_blobs_magic() {
  FakeClient clients[kMaxClients];
  memset(clients, 0, sizeof(clients));
  uint8_t blob[kCredBlobSize];
  record_blob::serialize(blob, sizeof(blob), 0x4d344153 /* "M4AS" */, kVersion, clients,
                         sizeof(FakeClient), kMaxClients, 1);

  FakeClient back[kMaxClients];
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kNoData,
                    record_blob::parse(blob, sizeof(blob), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
}

void test_rejects_a_different_version() {
  FakeClient clients[kMaxClients];
  memset(clients, 0, sizeof(clients));
  uint8_t blob[kCredBlobSize];
  record_blob::serialize(blob, sizeof(blob), kCredMagic, 2, clients, sizeof(FakeClient),
                         kMaxClients, 1);

  FakeClient back[kMaxClients];
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kNoData,
                    record_blob::parse(blob, sizeof(blob), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
}

// A single flipped bit anywhere in the payload must be caught -- this is what
// makes a half-finished write safe rather than silently wrong.
void test_rejects_a_corrupted_payload() {
  FakeClient clients[kMaxClients];
  memset(clients, 0, sizeof(clients));
  clients[0].secret[7] = 0x5A;

  uint8_t blob[kCredBlobSize];
  record_blob::serialize(blob, sizeof(blob), kCredMagic, kVersion, clients, sizeof(FakeClient),
                         kMaxClients, 1);
  blob[23] ^= 0x01;

  FakeClient back[kMaxClients];
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kBadChecksum,
                    record_blob::parse(blob, sizeof(blob), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
}

void test_rejects_an_impossible_count() {
  FakeClient clients[kMaxClients];
  memset(clients, 0, sizeof(clients));
  uint8_t blob[kCredBlobSize];
  record_blob::serialize(blob, sizeof(blob), kCredMagic, kVersion, clients, sizeof(FakeClient),
                         kMaxClients, 1);

  // Rewrite count past capacity and refresh the CRC, so only the range check
  // can catch it.
  blob[6] = 99;
  const uint32_t crc = record_blob::crc32(blob, kCredBlobSize - 4);
  memcpy(blob + kCredBlobSize - 4, &crc, 4);

  FakeClient back[kMaxClients];
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kBadCount,
                    record_blob::parse(blob, sizeof(blob), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
}

void test_rejects_a_short_buffer() {
  uint8_t tiny[16];
  memset(tiny, 0, sizeof(tiny));
  FakeClient back[kMaxClients];
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kTooShort,
                    record_blob::parse(tiny, sizeof(tiny), kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));

  uint8_t out[kCredBlobSize];
  TEST_ASSERT_FALSE(record_blob::serialize(out, 16, kCredMagic, kVersion, back,
                                           sizeof(FakeClient), kMaxClients, 0));
}

void test_rejects_more_records_than_capacity() {
  FakeClient clients[kMaxClients];
  uint8_t blob[kCredBlobSize];
  TEST_ASSERT_FALSE(record_blob::serialize(blob, sizeof(blob), kCredMagic, kVersion, clients,
                                           sizeof(FakeClient), kMaxClients, kMaxClients + 1));
}

// A failed parse must leave the caller's records alone: a corrupt read should
// not also destroy what is already in memory.
void test_failed_parse_leaves_records_untouched() {
  FakeClient back[kMaxClients];
  memset(back, 0, sizeof(back));
  back[0].pairedAt = 0xDEADBEEF;

  uint8_t blank[kCredBlobSize];
  memset(blank, 0xFF, sizeof(blank));
  uint16_t count = 7;
  record_blob::parse(blank, sizeof(blank), kCredMagic, kVersion, back, sizeof(FakeClient),
                     kMaxClients, count);

  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, back[0].pairedAt);
  TEST_ASSERT_EQUAL_UINT16(7, count);
}

// --- blob_store contract ---------------------------------------------------

void test_blob_store_round_trips_through_the_named_slot() {
  const uint8_t written[] = {1, 2, 3, 4, 5};
  TEST_ASSERT_TRUE(blob_store::save(blob_store::kCredentials, written, sizeof(written)));

  uint8_t read[sizeof(written)] = {};
  size_t length = 0;
  TEST_ASSERT_TRUE(blob_store::load(blob_store::kCredentials, read, sizeof(read), length));
  TEST_ASSERT_EQUAL_UINT32(sizeof(written), length);
  TEST_ASSERT_EQUAL_MEMORY(written, read, sizeof(written));
}

// The two blobs are independent storage: writing settings must never disturb
// credentials, which on a real board is why they get separate erase blocks.
void test_blobs_are_independent() {
  const uint8_t creds[] = {0xAA, 0xBB};
  const uint8_t settings[] = {0xCC, 0xDD};
  blob_store::save(blob_store::kCredentials, creds, sizeof(creds));
  blob_store::save(blob_store::kAppSettings, settings, sizeof(settings));

  uint8_t read[2] = {};
  size_t length = 0;
  blob_store::load(blob_store::kCredentials, read, sizeof(read), length);
  TEST_ASSERT_EQUAL_MEMORY(creds, read, sizeof(creds));
  blob_store::load(blob_store::kAppSettings, read, sizeof(read), length);
  TEST_ASSERT_EQUAL_MEMORY(settings, read, sizeof(settings));
}

void test_unknown_blob_name_fails_rather_than_writing_nowhere() {
  const uint8_t data[] = {1};
  TEST_ASSERT_FALSE(blob_store::save("nope", data, sizeof(data)));
  uint8_t read[1];
  size_t length = 0;
  TEST_ASSERT_FALSE(blob_store::load("nope", read, sizeof(read), length));
  TEST_ASSERT_FALSE(blob_store::checkPlacement("nope"));
}

// The whole point, end to end: bytes written through the seam parse back as
// the same records on the other side.
void test_records_survive_a_full_store_cycle() {
  FakeClient clients[kMaxClients];
  memset(clients, 0, sizeof(clients));
  memcpy(clients[0].id, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
  memset(clients[0].secret, 0x7E, sizeof(clients[0].secret));
  clients[0].pairedAt = 1788313768;

  uint8_t blob[kCredBlobSize];
  record_blob::serialize(blob, sizeof(blob), kCredMagic, kVersion, clients, sizeof(FakeClient),
                         kMaxClients, 1);
  TEST_ASSERT_TRUE(blob_store::save(blob_store::kCredentials, blob, sizeof(blob)));

  uint8_t reread[kCredBlobSize];
  size_t length = 0;
  TEST_ASSERT_TRUE(blob_store::load(blob_store::kCredentials, reread, sizeof(reread), length));

  FakeClient back[kMaxClients];
  memset(back, 0, sizeof(back));
  uint16_t count = 0;
  TEST_ASSERT_EQUAL(record_blob::ParseResult::kOk,
                    record_blob::parse(reread, length, kCredMagic, kVersion, back,
                                       sizeof(FakeClient), kMaxClients, count));
  TEST_ASSERT_EQUAL_UINT16(1, count);
  TEST_ASSERT_EQUAL_MEMORY(clients[0].id, back[0].id, 8);
  TEST_ASSERT_EQUAL_MEMORY(clients[0].secret, back[0].secret, 32);
  TEST_ASSERT_EQUAL_UINT32(1788313768, back[0].pairedAt);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_matches_the_standard_check_value);
  RUN_TEST(test_crc32_of_empty_input);
  RUN_TEST(test_framed_size_is_header_records_and_crc);
  RUN_TEST(test_serialized_layout_is_exact);
  RUN_TEST(test_serialization_is_deterministic);
  RUN_TEST(test_round_trips_records);
  RUN_TEST(test_round_trips_an_empty_store);
  RUN_TEST(test_round_trips_a_full_store);
  RUN_TEST(test_blank_flash_reads_as_no_data);
  RUN_TEST(test_rejects_another_blobs_magic);
  RUN_TEST(test_rejects_a_different_version);
  RUN_TEST(test_rejects_a_corrupted_payload);
  RUN_TEST(test_rejects_an_impossible_count);
  RUN_TEST(test_rejects_a_short_buffer);
  RUN_TEST(test_rejects_more_records_than_capacity);
  RUN_TEST(test_failed_parse_leaves_records_untouched);
  RUN_TEST(test_blob_store_round_trips_through_the_named_slot);
  RUN_TEST(test_blobs_are_independent);
  RUN_TEST(test_unknown_blob_name_fails_rather_than_writing_nowhere);
  RUN_TEST(test_records_survive_a_full_store_cycle);
  return UNITY_END();
}
