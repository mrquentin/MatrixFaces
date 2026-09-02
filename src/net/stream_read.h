#pragma once

#include <Client.h>

#include <cstddef>
#include <cstdint>

// One home for "pull bytes off a socket without hanging forever".
//
// Three call sites (HTTP request parsing, the MultiViewer fetch, the timezone
// lookup) each hand-rolled the same read/connected/available/delay dance. They
// differed only in when they gave up, and those differences are load-bearing --
// so they are the three named functions here rather than three copies of the
// loop.
namespace net {

enum class LineStatus : uint8_t {
  kOk,
  // The line did not fit `cap`. It has still been consumed through its
  // newline, so the caller stays in sync with the stream and can decide
  // whether an oversized line is fatal.
  kTruncated,
  kTimeout,
};

// Reads one CRLF- or LF-terminated line into `out`, always NUL-terminating and
// stripping the trailing CR. `timeoutMs` is a hard cap on the whole line: a
// peer that trickles an endless header gets cut off rather than holding the
// socket for as long as it keeps typing.
LineStatus readLine(Client &client, char *out, size_t cap, size_t &outLen, uint32_t timeoutMs);

// Reads exactly `want` bytes into `out`. Returns false if the peer closed or
// went silent for `timeoutMs` before delivering them.
bool readExactly(Client &client, char *out, size_t want, uint32_t timeoutMs);

// Reads until the peer closes or `out` holds `cap - 1` bytes, whichever comes
// first, and NUL-terminates. Returns the number of bytes stored.
//
// Here `timeoutMs` bounds *silence*, not the transfer: while the peer keeps
// sending, this keeps reading past it. That is deliberate -- it is what lets a
// 32 KB MultiViewer response complete over a slow link -- and it is why the
// caller must size `cap` for the largest response it is willing to hold.
size_t readUntilClose(Client &client, char *out, size_t cap, uint32_t timeoutMs);

// Discards whatever is already buffered, for at most `timeoutMs`, so the peer
// sees a clean close rather than a reset.
void drainBuffered(Client &client, uint32_t timeoutMs);

}  // namespace net
