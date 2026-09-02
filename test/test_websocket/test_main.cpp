#include <unity.h>

#include <cstring>

#include "api/websocket.h"

// RFC 6455's own examples where it gives them, and round trips where it does
// not. The handshake vector in particular is worth having: an accept key that
// is subtly wrong produces a browser that simply refuses to connect, with
// nothing on either side saying why.

void setUp() {}
void tearDown() {}

// --- handshake --------------------------------------------------------------

// RFC 6455 section 1.3: the key "dGhlIHNhbXBsZSBub25jZQ==" must produce
// "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=".
void test_accept_key_matches_the_rfc_example() {
  char accept[ws::kAcceptKeyCap + 1] = {};
  TEST_ASSERT_TRUE(ws::acceptKey("dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof(accept)));
  TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", accept);
}

void test_accept_key_refuses_a_short_buffer() {
  char accept[8] = {};
  TEST_ASSERT_FALSE(ws::acceptKey("dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof(accept)));
}

void test_accept_key_refuses_a_missing_key() {
  char accept[ws::kAcceptKeyCap + 1] = {};
  TEST_ASSERT_FALSE(ws::acceptKey(nullptr, accept, sizeof(accept)));
}

// --- parsing frames ---------------------------------------------------------

// RFC 6455 section 5.7: a masked "Hello" from a client.
void test_parses_the_rfc_masked_text_frame() {
  uint8_t frame[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d,
                     0x7f, 0x9f, 0x4d, 0x51, 0x58};

  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kOk, ws::parseHeader(frame, sizeof(frame), header));
  TEST_ASSERT_TRUE(header.fin);
  TEST_ASSERT_EQUAL(ws::Opcode::kText, header.opcode);
  TEST_ASSERT_TRUE(header.masked);
  TEST_ASSERT_EQUAL_UINT64(5, header.payloadLen);
  TEST_ASSERT_EQUAL_size_t(6, header.headerLen);

  uint8_t payload[5];
  memcpy(payload, frame + header.headerLen, sizeof(payload));
  ws::unmask(payload, sizeof(payload), header.maskKey);
  TEST_ASSERT_EQUAL_MEMORY("Hello", payload, 5);
}

// The same payload arriving in two reads. Unmasking is keyed on the offset
// into the payload, not the offset into the buffer, or the second half comes
// out as noise.
void test_unmasking_survives_a_split_payload() {
  uint8_t frame[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d,
                     0x7f, 0x9f, 0x4d, 0x51, 0x58};
  ws::FrameHeader header{};
  ws::parseHeader(frame, sizeof(frame), header);

  uint8_t payload[5];
  memcpy(payload, frame + header.headerLen, sizeof(payload));
  ws::unmask(payload, 2, header.maskKey, 0);
  ws::unmask(payload + 2, 3, header.maskKey, 2);
  TEST_ASSERT_EQUAL_MEMORY("Hello", payload, 5);
}

void test_incomplete_header_is_reported_not_guessed() {
  const uint8_t frame[] = {0x81, 0x85, 0x37};
  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kIncomplete, ws::parseHeader(frame, sizeof(frame), header));
  TEST_ASSERT_EQUAL(ws::ParseResult::kIncomplete, ws::parseHeader(frame, 1, header));
  TEST_ASSERT_EQUAL(ws::ParseResult::kIncomplete, ws::parseHeader(frame, 0, header));
}

void test_parses_a_16_bit_length() {
  // 0x7E, not 0xFE: the top bit of that byte is the mask flag, and a masked
  // frame needs four more bytes for the key before the header is complete.
  const uint8_t frame[] = {0x81, 0x7E, 0x01, 0x00};  // 256 bytes, unmasked
  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kOk, ws::parseHeader(frame, sizeof(frame), header));
  TEST_ASSERT_EQUAL_UINT64(256, header.payloadLen);
  TEST_ASSERT_EQUAL_size_t(4, header.headerLen);
  TEST_ASSERT_FALSE(header.masked);
}

// The mistake the test above originally made, now pinned: with the mask bit
// set, a 126-length header is not complete until its four key bytes arrive.
void test_masked_16_bit_header_needs_its_key() {
  const uint8_t frame[] = {0x81, 0xFE, 0x01, 0x00};
  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kIncomplete, ws::parseHeader(frame, sizeof(frame), header));

  const uint8_t whole[] = {0x81, 0xFE, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD};
  TEST_ASSERT_EQUAL(ws::ParseResult::kOk, ws::parseHeader(whole, sizeof(whole), header));
  TEST_ASSERT_TRUE(header.masked);
  TEST_ASSERT_EQUAL_UINT64(256, header.payloadLen);
  TEST_ASSERT_EQUAL_size_t(8, header.headerLen);
}

void test_parses_a_64_bit_length() {
  const uint8_t frame[] = {0x82, 0x7F, 0, 0, 0, 0, 0, 0x01, 0x00, 0x00};
  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kOk, ws::parseHeader(frame, sizeof(frame), header));
  TEST_ASSERT_EQUAL_UINT64(65536, header.payloadLen);
  TEST_ASSERT_EQUAL_size_t(10, header.headerLen);
}

// Reserved bits mean an extension was negotiated. None was, so this is a peer
// speaking a protocol we did not agree to.
void test_reserved_bits_are_rejected() {
  const uint8_t frame[] = {0xC1, 0x00};  // RSV1 set
  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kUnsupported, ws::parseHeader(frame, sizeof(frame), header));
}

// A control frame is at most 125 bytes and never fragmented; both are protocol
// errors rather than cases to handle.
void test_oversized_control_frame_is_rejected() {
  const uint8_t frame[] = {0x89, 0xFE, 0x01, 0x00};  // ping, 256 bytes
  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kUnsupported, ws::parseHeader(frame, sizeof(frame), header));
}

void test_fragmented_control_frame_is_rejected() {
  const uint8_t frame[] = {0x09, 0x00};  // ping without FIN
  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kUnsupported, ws::parseHeader(frame, sizeof(frame), header));
}

void test_close_and_ping_opcodes_are_recognised() {
  const uint8_t close[] = {0x88, 0x00};
  const uint8_t ping[] = {0x89, 0x00};
  ws::FrameHeader header{};

  ws::parseHeader(close, sizeof(close), header);
  TEST_ASSERT_EQUAL(ws::Opcode::kClose, header.opcode);
  ws::parseHeader(ping, sizeof(ping), header);
  TEST_ASSERT_EQUAL(ws::Opcode::kPing, header.opcode);
}

// --- writing frames ---------------------------------------------------------

void test_short_text_header_is_two_bytes_and_unmasked() {
  uint8_t out[ws::kMaxServerHeader];
  TEST_ASSERT_EQUAL_size_t(2, ws::encodeHeader(out, sizeof(out), ws::Opcode::kText, 5));
  TEST_ASSERT_EQUAL_HEX8(0x81, out[0]);  // FIN + text
  TEST_ASSERT_EQUAL_HEX8(0x05, out[1]);  // mask bit clear: server frames never mask
}

void test_medium_text_header_uses_the_16_bit_form() {
  uint8_t out[ws::kMaxServerHeader];
  TEST_ASSERT_EQUAL_size_t(4, ws::encodeHeader(out, sizeof(out), ws::Opcode::kText, 300));
  TEST_ASSERT_EQUAL_HEX8(0x81, out[0]);
  TEST_ASSERT_EQUAL_HEX8(126, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x01, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x2C, out[3]);
}

void test_the_boundary_between_header_forms() {
  uint8_t out[ws::kMaxServerHeader];
  TEST_ASSERT_EQUAL_size_t(2, ws::encodeHeader(out, sizeof(out), ws::Opcode::kText, 125));
  TEST_ASSERT_EQUAL_size_t(4, ws::encodeHeader(out, sizeof(out), ws::Opcode::kText, 126));
}

// Refused rather than silently truncated: nothing here sends anything that
// large, and refusing is what keeps kMaxServerHeader honest.
void test_a_64_bit_length_is_refused() {
  uint8_t out[ws::kMaxServerHeader];
  TEST_ASSERT_EQUAL_size_t(0, ws::encodeHeader(out, sizeof(out), ws::Opcode::kText, 0x10000));
}

void test_encode_refuses_a_short_buffer() {
  uint8_t out[3];
  TEST_ASSERT_EQUAL_size_t(0, ws::encodeHeader(out, 1, ws::Opcode::kText, 5));
  TEST_ASSERT_EQUAL_size_t(0, ws::encodeHeader(out, sizeof(out), ws::Opcode::kText, 300));
}

void test_close_frame_carries_the_status_code() {
  uint8_t out[ws::kMaxControlFrame];
  TEST_ASSERT_EQUAL_size_t(4, ws::encodeClose(out, sizeof(out), 1000));
  TEST_ASSERT_EQUAL_HEX8(0x88, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x02, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x03, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xE8, out[3]);
}

void test_pong_echoes_the_ping_payload() {
  const uint8_t payload[] = {'a', 'b', 'c'};
  uint8_t out[ws::kMaxControlFrame];
  TEST_ASSERT_EQUAL_size_t(5, ws::encodePong(out, sizeof(out), payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_HEX8(0x8A, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x03, out[1]);
  TEST_ASSERT_EQUAL_MEMORY(payload, out + 2, sizeof(payload));
}

void test_pong_refuses_an_oversized_payload() {
  uint8_t payload[126] = {};
  uint8_t out[256];
  TEST_ASSERT_EQUAL_size_t(0, ws::encodePong(out, sizeof(out), payload, sizeof(payload)));
}

// What the board actually does: write a header, then the text after it, and
// read the result back as a client would.
void test_a_written_frame_parses_back() {
  const char *message = "{\"app\":\"clock\",\"key\":\"size\"}";
  const size_t textLen = strlen(message);

  uint8_t frame[64];
  const size_t headerLen = ws::encodeHeader(frame, sizeof(frame), ws::Opcode::kText, textLen);
  TEST_ASSERT_TRUE(headerLen > 0);
  memcpy(frame + headerLen, message, textLen);

  ws::FrameHeader header{};
  TEST_ASSERT_EQUAL(ws::ParseResult::kOk,
                    ws::parseHeader(frame, headerLen + textLen, header));
  TEST_ASSERT_TRUE(header.fin);
  TEST_ASSERT_FALSE(header.masked);
  TEST_ASSERT_EQUAL_UINT64(textLen, header.payloadLen);
  TEST_ASSERT_EQUAL_MEMORY(message, frame + header.headerLen, textLen);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_accept_key_matches_the_rfc_example);
  RUN_TEST(test_accept_key_refuses_a_short_buffer);
  RUN_TEST(test_accept_key_refuses_a_missing_key);
  RUN_TEST(test_parses_the_rfc_masked_text_frame);
  RUN_TEST(test_unmasking_survives_a_split_payload);
  RUN_TEST(test_incomplete_header_is_reported_not_guessed);
  RUN_TEST(test_parses_a_16_bit_length);
  RUN_TEST(test_masked_16_bit_header_needs_its_key);
  RUN_TEST(test_parses_a_64_bit_length);
  RUN_TEST(test_reserved_bits_are_rejected);
  RUN_TEST(test_oversized_control_frame_is_rejected);
  RUN_TEST(test_fragmented_control_frame_is_rejected);
  RUN_TEST(test_close_and_ping_opcodes_are_recognised);
  RUN_TEST(test_short_text_header_is_two_bytes_and_unmasked);
  RUN_TEST(test_medium_text_header_uses_the_16_bit_form);
  RUN_TEST(test_the_boundary_between_header_forms);
  RUN_TEST(test_a_64_bit_length_is_refused);
  RUN_TEST(test_encode_refuses_a_short_buffer);
  RUN_TEST(test_close_frame_carries_the_status_code);
  RUN_TEST(test_pong_echoes_the_ping_payload);
  RUN_TEST(test_pong_refuses_an_oversized_payload);
  RUN_TEST(test_a_written_frame_parses_back);
  return UNITY_END();
}
