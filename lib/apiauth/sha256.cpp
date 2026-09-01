#include "sha256.h"

#include <cstring>

namespace apiauth {
namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

void Sha256::reset() {
  state_[0] = 0x6a09e667U;
  state_[1] = 0xbb67ae85U;
  state_[2] = 0x3c6ef372U;
  state_[3] = 0xa54ff53aU;
  state_[4] = 0x510e527fU;
  state_[5] = 0x9b05688cU;
  state_[6] = 0x1f83d9abU;
  state_[7] = 0x5be0cd19U;
  bitLength_ = 0;
  bufferLen_ = 0;
}

// The round functions shift and xor values that integer promotion widens to
// int; every operand here is unsigned by construction.
// NOLINTBEGIN(hicpp-signed-bitwise)
void Sha256::compress(const uint8_t block[kBlockSize]) {
  uint32_t w[64];
  for (unsigned i = 0; i < 16; ++i) {
    w[i] = static_cast<uint32_t>(block[i * 4]) << 24 |
           static_cast<uint32_t>(block[i * 4 + 1]) << 16 |
           static_cast<uint32_t>(block[i * 4 + 2]) << 8 |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (unsigned i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state_[0];
  uint32_t b = state_[1];
  uint32_t c = state_[2];
  uint32_t d = state_[3];
  uint32_t e = state_[4];
  uint32_t f = state_[5];
  uint32_t g = state_[6];
  uint32_t h = state_[7];

  for (unsigned i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + s1 + ch + kK[i] + w[i];
    const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}
// NOLINTEND(hicpp-signed-bitwise)

void Sha256::update(const void *data, size_t len) {
  const auto *p = static_cast<const uint8_t *>(data);
  bitLength_ += static_cast<uint64_t>(len) * 8;

  while (len > 0) {
    const size_t room = kBlockSize - bufferLen_;
    const size_t take = len < room ? len : room;
    memcpy(buffer_ + bufferLen_, p, take);
    bufferLen_ += take;
    p += take;
    len -= take;

    if (bufferLen_ == kBlockSize) {
      compress(buffer_);
      bufferLen_ = 0;
    }
  }
}

void Sha256::finish(uint8_t out[kDigestSize]) {
  const uint64_t bits = bitLength_;

  constexpr uint8_t pad = 0x80;
  update(&pad, 1);
  constexpr uint8_t zero = 0x00;
  while (bufferLen_ != 56) {
    update(&zero, 1);
  }

  uint8_t lengthBytes[8];
  for (unsigned i = 0; i < 8; ++i) {
    lengthBytes[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
  }
  // Feed the length directly: update() would corrupt bitLength_, which we have
  // already captured above.
  memcpy(buffer_ + bufferLen_, lengthBytes, 8);
  compress(buffer_);
  bufferLen_ = 0;

  // NOLINTBEGIN(hicpp-signed-bitwise)
  for (unsigned i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
  }
  // NOLINTEND(hicpp-signed-bitwise)
}

void Sha256::hash(const void *data, size_t len, uint8_t out[kDigestSize]) {
  Sha256 ctx;
  ctx.update(data, len);
  ctx.finish(out);
}

}  // namespace apiauth
