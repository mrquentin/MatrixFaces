#include "api/websocket.h"

#include <cstring>

namespace ws {
namespace {

// --- SHA-1 ------------------------------------------------------------------
//
// Here and not in lib/apiauth on purpose: see the header. The handshake is the
// only thing that needs it, and RFC 6455 specifies it as a fixed part of the
// protocol rather than as a security property -- the accept key proves the
// peer understood the request, nothing more.

struct Sha1 {
  uint32_t state[5];
  uint64_t bitCount;
  uint8_t block[64];
  size_t blockLen;
};

uint32_t rol(uint32_t value, uint8_t bits) { return (value << bits) | (value >> (32 - bits)); }

void sha1Transform(Sha1 &ctx, const uint8_t *block) {
  uint32_t w[80];
  for (size_t i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (size_t i = 16; i < 80; ++i) {
    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }

  uint32_t a = ctx.state[0];
  uint32_t b = ctx.state[1];
  uint32_t c = ctx.state[2];
  uint32_t d = ctx.state[3];
  uint32_t e = ctx.state[4];

  for (size_t i = 0; i < 80; ++i) {
    uint32_t f;
    uint32_t k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }
    const uint32_t temp = rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rol(b, 30);
    b = a;
    a = temp;
  }

  ctx.state[0] += a;
  ctx.state[1] += b;
  ctx.state[2] += c;
  ctx.state[3] += d;
  ctx.state[4] += e;
}

void sha1Init(Sha1 &ctx) {
  ctx.state[0] = 0x67452301;
  ctx.state[1] = 0xEFCDAB89;
  ctx.state[2] = 0x98BADCFE;
  ctx.state[3] = 0x10325476;
  ctx.state[4] = 0xC3D2E1F0;
  ctx.bitCount = 0;
  ctx.blockLen = 0;
}

void sha1Update(Sha1 &ctx, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    ctx.block[ctx.blockLen++] = data[i];
    ctx.bitCount += 8;
    if (ctx.blockLen == sizeof(ctx.block)) {
      sha1Transform(ctx, ctx.block);
      ctx.blockLen = 0;
    }
  }
}

void sha1Final(Sha1 &ctx, uint8_t out[20]) {
  const uint64_t bits = ctx.bitCount;

  uint8_t pad = 0x80;
  sha1Update(ctx, &pad, 1);
  pad = 0x00;
  while (ctx.blockLen != 56) {
    sha1Update(ctx, &pad, 1);
  }

  uint8_t length[8];
  for (size_t i = 0; i < 8; ++i) {
    length[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
  }
  // Written straight into the block: sha1Update would count these as message
  // bytes, which would corrupt the very length being appended.
  memcpy(ctx.block + 56, length, sizeof(length));
  sha1Transform(ctx, ctx.block);

  for (size_t i = 0; i < 20; ++i) {
    out[i] = static_cast<uint8_t>(ctx.state[i / 4] >> (24 - (i % 4) * 8));
  }
}

// --- Base64 (encode only) ---------------------------------------------------

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64Encode(const uint8_t *in, size_t len, char *out, size_t cap) {
  const size_t needed = ((len + 2) / 3) * 4;
  if (cap < needed + 1) return 0;

  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t a = in[i];
    const uint32_t b = i + 1 < len ? in[i + 1] : 0;
    const uint32_t c = i + 2 < len ? in[i + 2] : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;

    out[o++] = kBase64[(triple >> 18) & 0x3F];
    out[o++] = kBase64[(triple >> 12) & 0x3F];
    out[o++] = i + 1 < len ? kBase64[(triple >> 6) & 0x3F] : '=';
    out[o++] = i + 2 < len ? kBase64[triple & 0x3F] : '=';
  }
  out[o] = '\0';
  return o;
}

// The magic string RFC 6455 appends to the client's key. Not a secret; it
// exists so a server that echoes the key cannot be mistaken for one that
// implements the protocol.
constexpr char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

}  // namespace

bool acceptKey(const char *clientKey, char *out, size_t cap) {
  if (clientKey == nullptr || out == nullptr || cap < kAcceptKeyCap + 1) return false;

  Sha1 ctx;
  sha1Init(ctx);
  sha1Update(ctx, reinterpret_cast<const uint8_t *>(clientKey), strlen(clientKey));
  sha1Update(ctx, reinterpret_cast<const uint8_t *>(kGuid), sizeof(kGuid) - 1);

  uint8_t digest[20];
  sha1Final(ctx, digest);
  return base64Encode(digest, sizeof(digest), out, cap) != 0;
}

ParseResult parseHeader(const uint8_t *data, size_t len, FrameHeader &out) {
  if (len < 2) return ParseResult::kIncomplete;

  const uint8_t first = data[0];
  const uint8_t second = data[1];

  // The three reserved bits are only meaningful with an extension negotiated,
  // and none is. A peer setting them is speaking a protocol we did not agree
  // to, so say so rather than ignore them.
  if ((first & 0x70) != 0) return ParseResult::kUnsupported;

  out.fin = (first & 0x80) != 0;
  out.opcode = static_cast<Opcode>(first & 0x0F);
  out.masked = (second & 0x80) != 0;

  const uint8_t lenField = second & 0x7F;
  size_t cursor = 2;

  if (lenField < 126) {
    out.payloadLen = lenField;
  } else if (lenField == 126) {
    if (len < cursor + 2) return ParseResult::kIncomplete;
    out.payloadLen = (static_cast<uint64_t>(data[cursor]) << 8) | data[cursor + 1];
    cursor += 2;
  } else {
    if (len < cursor + 8) return ParseResult::kIncomplete;
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
      value = (value << 8) | data[cursor + i];
    }
    // The top bit must be clear per the RFC; beyond that the caller decides
    // what it can hold, and reports a payload it cannot take as unsupported.
    if ((value >> 63) != 0) return ParseResult::kUnsupported;
    out.payloadLen = value;
    cursor += 8;
  }

  // A control frame carries at most 125 bytes and is never fragmented. Both
  // are protocol errors rather than something to handle.
  const bool isControl = (static_cast<uint8_t>(out.opcode) & 0x08) != 0;
  if (isControl && (out.payloadLen > 125 || !out.fin)) return ParseResult::kUnsupported;

  if (out.masked) {
    if (len < cursor + 4) return ParseResult::kIncomplete;
    memcpy(out.maskKey, data + cursor, 4);
    cursor += 4;
  } else {
    memset(out.maskKey, 0, sizeof(out.maskKey));
  }

  out.headerLen = cursor;
  return ParseResult::kOk;
}

void unmask(uint8_t *payload, size_t len, const uint8_t maskKey[4], uint64_t offset) {
  for (size_t i = 0; i < len; ++i) {
    payload[i] ^= maskKey[(offset + i) % 4];
  }
}

size_t encodeHeader(uint8_t *out, size_t cap, Opcode opcode, size_t payloadLen) {
  if (out == nullptr) return 0;

  // FIN always set: this implementation does not fragment, so every frame it
  // writes is complete on its own.
  const auto first = static_cast<uint8_t>(0x80 | static_cast<uint8_t>(opcode));

  if (payloadLen < 126) {
    if (cap < 2) return 0;
    out[0] = first;
    out[1] = static_cast<uint8_t>(payloadLen);
    return 2;
  }
  if (payloadLen <= 0xFFFF) {
    if (cap < 4) return 0;
    out[0] = first;
    out[1] = 126;
    out[2] = static_cast<uint8_t>(payloadLen >> 8);
    out[3] = static_cast<uint8_t>(payloadLen);
    return 4;
  }
  // A 64-bit length is legal but nothing here produces one: the largest thing
  // the board sends is a settings document. Refusing keeps kMaxServerHeader
  // honest rather than sizing every caller's buffer for a case that cannot
  // happen.
  return 0;
}

size_t encodeClose(uint8_t *out, size_t cap, uint16_t code) {
  if (cap < 4) return 0;
  out[0] = 0x80 | static_cast<uint8_t>(Opcode::kClose);
  out[1] = 2;
  out[2] = static_cast<uint8_t>(code >> 8);
  out[3] = static_cast<uint8_t>(code);
  return 4;
}

size_t encodePong(uint8_t *out, size_t cap, const uint8_t *payload, size_t payloadLen) {
  if (payloadLen > 125) return 0;  // control frames cannot be longer
  if (cap < 2 + payloadLen) return 0;

  out[0] = 0x80 | static_cast<uint8_t>(Opcode::kPong);
  out[1] = static_cast<uint8_t>(payloadLen);
  if (payloadLen > 0 && payload != nullptr) memcpy(out + 2, payload, payloadLen);
  return 2 + payloadLen;
}

}  // namespace ws
