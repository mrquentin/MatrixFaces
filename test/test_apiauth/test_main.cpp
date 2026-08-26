// Known-answer tests for the pure-C++ auth primitives.
//
//   pio test -e adafruit_matrix_portal_m4     (runs on the board, reports over serial)
//   pio test -e native                        (needs a host g++/clang++ on PATH)
//
// SHA-256 digests and the HMAC cases come from FIPS 180-4 and RFC 4231; the
// request-signature vectors were generated with Python's hashlib/hmac.

#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "auth_header.h"
#include "hex.h"
#include "hmac_sha256.h"
#include "request_sig.h"
#include "sha256.h"

using namespace apiauth;

namespace {

void assertDigestHex(const uint8_t digest[32], const char *expected) {
  char actual[65];
  toHex(digest, 32, actual);
  TEST_ASSERT_EQUAL_STRING(expected, actual);
}

const uint8_t kTestSecret[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

}  // namespace

void test_sha256_empty(void) {
  uint8_t digest[32];
  Sha256::hash("", 0, digest);
  assertDigestHex(digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

void test_sha256_abc(void) {
  uint8_t digest[32];
  Sha256::hash("abc", 3, digest);
  assertDigestHex(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// 56 bytes lands exactly on the padding boundary that forces a second block.
void test_sha256_padding_boundary(void) {
  const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  TEST_ASSERT_EQUAL_UINT32(56, (uint32_t)strlen(msg));

  uint8_t digest[32];
  Sha256::hash(msg, strlen(msg), digest);
  assertDigestHex(digest, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

void test_sha256_multi_block(void) {
  uint8_t msg[200];
  memset(msg, 'a', sizeof(msg));

  uint8_t digest[32];
  Sha256::hash(msg, sizeof(msg), digest);
  assertDigestHex(digest, "c2a908d98f5df987ade41b5fce213067efbcc21ef2240212a41e54b5e7c28ae5");
}

// Feeding the same bytes in awkward chunk sizes must not change the result.
void test_sha256_incremental_matches_oneshot(void) {
  uint8_t msg[200];
  for (size_t i = 0; i < sizeof(msg); ++i) {
    msg[i] = (uint8_t)(i * 7 + 3);
  }

  uint8_t oneShot[32];
  Sha256::hash(msg, sizeof(msg), oneShot);

  const size_t chunkSizes[] = {1, 7, 63, 64, 65};
  for (size_t c = 0; c < sizeof(chunkSizes) / sizeof(chunkSizes[0]); ++c) {
    Sha256 ctx;
    size_t offset = 0;
    while (offset < sizeof(msg)) {
      size_t take = chunkSizes[c];
      if (offset + take > sizeof(msg)) take = sizeof(msg) - offset;
      ctx.update(msg + offset, take);
      offset += take;
    }
    uint8_t streamed[32];
    ctx.finish(streamed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(oneShot, streamed, 32);
  }
}

void test_hmac_rfc4231_case1(void) {
  uint8_t key[20];
  memset(key, 0x0b, sizeof(key));

  uint8_t mac[32];
  hmacSha256(key, sizeof(key), "Hi There", 8, mac);
  assertDigestHex(mac, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

void test_hmac_rfc4231_case2(void) {
  uint8_t mac[32];
  const char *data = "what do ya want for nothing?";
  hmacSha256("Jefe", 4, data, strlen(data), mac);
  assertDigestHex(mac, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// 131-byte key exercises the "hash the key first" branch.
void test_hmac_rfc4231_case6_long_key(void) {
  uint8_t key[131];
  memset(key, 0xaa, sizeof(key));

  const char *data = "Test Using Larger Than Block-Size Key - Hash Key First";
  uint8_t mac[32];
  hmacSha256(key, sizeof(key), data, strlen(data), mac);
  assertDigestHex(mac, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

void test_hex_roundtrip(void) {
  const uint8_t bytes[4] = {0x00, 0x0f, 0xa5, 0xff};
  char hex[9];
  toHex(bytes, sizeof(bytes), hex);
  TEST_ASSERT_EQUAL_STRING("000fa5ff", hex);

  uint8_t decoded[4];
  TEST_ASSERT_TRUE(fromHex(hex, 8, decoded, sizeof(decoded)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, decoded, sizeof(bytes));

  TEST_ASSERT_TRUE(fromHex("000FA5FF", 8, decoded, sizeof(decoded)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, decoded, sizeof(bytes));

  TEST_ASSERT_FALSE(fromHex("00g0", 4, decoded, 2));   // not hex
  TEST_ASSERT_FALSE(fromHex("0011", 4, decoded, 4));   // wrong length
}

void test_canonical_request_layout(void) {
  uint8_t bodyHash[32];
  Sha256::hash("", 0, bodyHash);

  char canonical[kCanonicalRequestMax];
  const size_t len = buildCanonicalRequest("GET", "/api/status", "1700000000",
                                           "0011223344556677", bodyHash, canonical,
                                           sizeof(canonical));
  TEST_ASSERT_EQUAL_UINT32(108, (uint32_t)len);
  TEST_ASSERT_EQUAL_STRING(
      "GET\n/api/status\n1700000000\n0011223344556677\n"
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      canonical);
}

void test_canonical_request_rejects_overflow(void) {
  uint8_t bodyHash[32];
  Sha256::hash("", 0, bodyHash);

  char tiny[16];
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)buildCanonicalRequest("GET", "/api/status", "1700000000",
                                                              "0011223344556677", bodyHash, tiny,
                                                              sizeof(tiny)));
}

void test_sign_request_get(void) {
  uint8_t mac[32];
  TEST_ASSERT_TRUE(signRequest(kTestSecret, sizeof(kTestSecret), "GET", "/api/status",
                               "1700000000", "0011223344556677", "", 0, mac));
  assertDigestHex(mac, "f7b2089f804512fe2d2f9793e35f28423118ef337c73f73f08fcaa0cefb3a6ce");
}

void test_sign_request_post_with_body(void) {
  const char *body = "{\"on\":true}";
  uint8_t mac[32];
  TEST_ASSERT_TRUE(signRequest(kTestSecret, sizeof(kTestSecret), "POST", "/api/led", "1700000123",
                               "89abcdef01234567", body, strlen(body), mac));
  assertDigestHex(mac, "a6ea34944a1ff180c29f80f54f15dcc13d815b2a3c96881cf7e0722debf1e226");
}

void test_verify_accepts_valid_signature(void) {
  TEST_ASSERT_TRUE(verifyRequestSignature(
      kTestSecret, sizeof(kTestSecret), "GET", "/api/status", "1700000000", "0011223344556677", "",
      0, "f7b2089f804512fe2d2f9793e35f28423118ef337c73f73f08fcaa0cefb3a6ce"));
}

// Every signed field must actually change the signature.
void test_verify_rejects_tampering(void) {
  const char *sig = "f7b2089f804512fe2d2f9793e35f28423118ef337c73f73f08fcaa0cefb3a6ce";

  TEST_ASSERT_FALSE(verifyRequestSignature(kTestSecret, sizeof(kTestSecret), "POST", "/api/status",
                                           "1700000000", "0011223344556677", "", 0, sig));
  TEST_ASSERT_FALSE(verifyRequestSignature(kTestSecret, sizeof(kTestSecret), "GET", "/api/reboot",
                                           "1700000000", "0011223344556677", "", 0, sig));
  TEST_ASSERT_FALSE(verifyRequestSignature(kTestSecret, sizeof(kTestSecret), "GET", "/api/status",
                                           "1700000001", "0011223344556677", "", 0, sig));
  TEST_ASSERT_FALSE(verifyRequestSignature(kTestSecret, sizeof(kTestSecret), "GET", "/api/status",
                                           "1700000000", "0011223344556678", "", 0, sig));
  TEST_ASSERT_FALSE(verifyRequestSignature(kTestSecret, sizeof(kTestSecret), "GET", "/api/status",
                                           "1700000000", "0011223344556677", "x", 1, sig));

  uint8_t otherSecret[32];
  memcpy(otherSecret, kTestSecret, sizeof(otherSecret));
  otherSecret[31] ^= 0x01;
  TEST_ASSERT_FALSE(verifyRequestSignature(otherSecret, sizeof(otherSecret), "GET", "/api/status",
                                           "1700000000", "0011223344556677", "", 0, sig));

  TEST_ASSERT_FALSE(verifyRequestSignature(kTestSecret, sizeof(kTestSecret), "GET", "/api/status",
                                           "1700000000", "0011223344556677", "", 0, "deadbeef"));
}

void test_parse_auth_header(void) {
  AuthHeader header;
  TEST_ASSERT_TRUE(parseAuthHeader(
      "HMAC id=0011223344556677,ts=1700000000,nonce=89abcdef01234567,"
      "sig=f7b2089f804512fe2d2f9793e35f28423118ef337c73f73f08fcaa0cefb3a6ce",
      header));
  TEST_ASSERT_EQUAL_STRING("0011223344556677", header.id);
  TEST_ASSERT_EQUAL_STRING("1700000000", header.ts);
  TEST_ASSERT_EQUAL_UINT32(1700000000u, header.timestamp);
  TEST_ASSERT_EQUAL_STRING("89abcdef01234567", header.nonce);
  TEST_ASSERT_EQUAL_STRING("f7b2089f804512fe2d2f9793e35f28423118ef337c73f73f08fcaa0cefb3a6ce",
                           header.sig);
}

void test_parse_auth_header_tolerates_formatting(void) {
  AuthHeader header;
  TEST_ASSERT_TRUE(parseAuthHeader(
      "  hmac  ts=42, nonce=89abcdef01234567 , id=0011223344556677, "
      "sig=f7b2089f804512fe2d2f9793e35f28423118ef337c73f73f08fcaa0cefb3a6ce",
      header));
  TEST_ASSERT_EQUAL_STRING("42", header.ts);
  TEST_ASSERT_EQUAL_UINT32(42u, header.timestamp);
  TEST_ASSERT_EQUAL_STRING("0011223344556677", header.id);
}

void test_parse_auth_header_rejects_malformed(void) {
  AuthHeader header;
  const char *kValidSig = "sig=f7b2089f804512fe2d2f9793e35f28423118ef337c73f73f08fcaa0cefb3a6ce";
  char buffer[256];

  TEST_ASSERT_FALSE(parseAuthHeader(NULL, header));
  TEST_ASSERT_FALSE(parseAuthHeader("", header));
  TEST_ASSERT_FALSE(parseAuthHeader("Bearer abc123", header));

  // Missing sig.
  TEST_ASSERT_FALSE(
      parseAuthHeader("HMAC id=0011223344556677,ts=1,nonce=89abcdef01234567", header));

  // Short id.
  snprintf(buffer, sizeof(buffer), "HMAC id=00112233,ts=1,nonce=89abcdef01234567,%s", kValidSig);
  TEST_ASSERT_FALSE(parseAuthHeader(buffer, header));

  // Non-hex id.
  snprintf(buffer, sizeof(buffer), "HMAC id=zz11223344556677,ts=1,nonce=89abcdef01234567,%s",
           kValidSig);
  TEST_ASSERT_FALSE(parseAuthHeader(buffer, header));

  // Non-numeric timestamp.
  snprintf(buffer, sizeof(buffer), "HMAC id=0011223344556677,ts=abc,nonce=89abcdef01234567,%s",
           kValidSig);
  TEST_ASSERT_FALSE(parseAuthHeader(buffer, header));

  // Timestamp beyond 32 bits.
  snprintf(buffer, sizeof(buffer),
           "HMAC id=0011223344556677,ts=99999999999,nonce=89abcdef01234567,%s", kValidSig);
  TEST_ASSERT_FALSE(parseAuthHeader(buffer, header));

  // Unknown parameter: not covered by the signature, so it must not be ignored.
  snprintf(buffer, sizeof(buffer),
           "HMAC id=0011223344556677,ts=1,nonce=89abcdef01234567,%s,extra=1", kValidSig);
  TEST_ASSERT_FALSE(parseAuthHeader(buffer, header));
}

void setUp(void) {}
void tearDown(void) {}

static int runAllTests() {
  UNITY_BEGIN();
  RUN_TEST(test_sha256_empty);
  RUN_TEST(test_sha256_abc);
  RUN_TEST(test_sha256_padding_boundary);
  RUN_TEST(test_sha256_multi_block);
  RUN_TEST(test_sha256_incremental_matches_oneshot);
  RUN_TEST(test_hmac_rfc4231_case1);
  RUN_TEST(test_hmac_rfc4231_case2);
  RUN_TEST(test_hmac_rfc4231_case6_long_key);
  RUN_TEST(test_hex_roundtrip);
  RUN_TEST(test_canonical_request_layout);
  RUN_TEST(test_canonical_request_rejects_overflow);
  RUN_TEST(test_sign_request_get);
  RUN_TEST(test_sign_request_post_with_body);
  RUN_TEST(test_verify_accepts_valid_signature);
  RUN_TEST(test_verify_rejects_tampering);
  RUN_TEST(test_parse_auth_header);
  RUN_TEST(test_parse_auth_header_tolerates_formatting);
  RUN_TEST(test_parse_auth_header_rejects_malformed);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>

void setup() {
  // Give the USB serial monitor time to attach before the results scroll past.
  delay(2000);
  runAllTests();
}

void loop() {}
#else
int main() { return runAllTests(); }
#endif
