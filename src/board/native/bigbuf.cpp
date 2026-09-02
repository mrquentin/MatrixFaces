#include "board/bigbuf.h"

// The host's copy. Statically reserved like the SAMD51's, and the same size,
// so a test that fills it exercises the smaller of the two real boards -- the
// one where overrunning it is a live concern.
namespace {

constexpr size_t kResponseCap = 32768;
char g_response[kResponseCap];

}  // namespace

namespace bigbuf {

char *responseBuffer(size_t &outCap) {
  outCap = sizeof(g_response);
  return g_response;
}

}  // namespace bigbuf
