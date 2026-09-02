#pragma once

#include <cstddef>
#include <cstdint>

// RFC 6455, the part of it this firmware needs: the handshake's accept key,
// and reading and writing single unfragmented frames.
//
// Deliberately not a WebSocket library. No fragmentation, no extensions, no
// compression, no client role. What is here is what a browser needs to hold a
// connection open and exchange small JSON messages with the board, and every
// piece of it is reachable from the host tests -- the functions take buffers,
// not sockets, so test_websocket can put RFC vectors through them.
//
// SHA-1 and Base64 live inside websocket.cpp rather than in lib/apiauth. That
// library is the signing path and is audited as such; the handshake needs a
// hash that is broken for signing and is used here only because the RFC names
// it, which is not a thing to put next to the code that authenticates
// requests.
namespace ws {

// The value for the Sec-WebSocket-Accept response header, computed from the
// client's Sec-WebSocket-Key. Writes at most 29 bytes plus a terminator.
// False if `out` is too small or the key is missing.
constexpr size_t kAcceptKeyCap = 29;
bool acceptKey(const char *clientKey, char *out, size_t cap);

enum class Opcode : uint8_t {
  kContinuation = 0x0,
  kText = 0x1,
  kBinary = 0x2,
  kClose = 0x8,
  kPing = 0x9,
  kPong = 0xA,
};

struct FrameHeader {
  bool fin;
  Opcode opcode;
  bool masked;
  // 64-bit because the wire format allows it. The caller decides what it is
  // willing to hold; nothing here allocates.
  uint64_t payloadLen;
  uint8_t maskKey[4];
  // How many bytes of `data` the header used, so the payload starts there.
  size_t headerLen;
};

enum class ParseResult : uint8_t {
  kOk,
  // Fewer bytes than the header needs. Read more and call again; nothing was
  // consumed.
  kIncomplete,
  // Well-formed but not something this implementation handles -- a 64-bit
  // length beyond what the caller could hold, or a reserved bit set. The
  // caller should close the connection rather than guess.
  kUnsupported,
};

ParseResult parseHeader(const uint8_t *data, size_t len, FrameHeader &out);

// Unmasks in place. Every frame from a client is masked; a server frame never
// is. `offset` is how many payload bytes were already unmasked, so a payload
// arriving in pieces still gets the right key byte applied to each.
void unmask(uint8_t *payload, size_t len, const uint8_t maskKey[4], uint64_t offset = 0);

// Server-to-client frames, written unmasked as the RFC requires. Each returns
// the number of bytes written, or 0 if `cap` was too small.
//
// The header is written separately from the payload so a large text frame can
// be streamed to the socket without a second copy of it in RAM.
constexpr size_t kMaxServerHeader = 4;
size_t encodeHeader(uint8_t *out, size_t cap, Opcode opcode, size_t payloadLen);

// A close frame carrying a status code, and a pong echoing a ping's payload.
// Both are small enough to build whole.
constexpr size_t kMaxControlFrame = 2 + 125;
size_t encodeClose(uint8_t *out, size_t cap, uint16_t code);
size_t encodePong(uint8_t *out, size_t cap, const uint8_t *payload, size_t payloadLen);

}  // namespace ws
