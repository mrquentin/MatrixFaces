#pragma once

#include <cstddef>

// The firmware's one genuinely large allocation, handed out by the board.
//
// Only the MultiViewer response needs it, and it dominates everything else:
// 32 KB is 17% of the M4's RAM. That made it worth moving out of a
// function-local static and into the composition root in phase 1.3, so a build
// without the F1 app would not carry it -- and worth moving again now, because
// *where* it can live is the one thing the two boards disagree about. The M4
// has only its SRAM. The S3 has 2 MB of PSRAM doing nothing, so it can hold a
// bigger buffer and give the internal heap back to everything else.
namespace bigbuf {

// Returns the response buffer and its capacity, or nullptr with `outCap` zero
// if the board could not provide one.
//
// Call once, from the variant that needs it. A null return is a real
// possibility on a board that allocates rather than reserving -- and the
// caller's answer is to not register the app that needed it, rather than to
// carry on with a buffer it does not have.
char *responseBuffer(size_t &outCap);

}  // namespace bigbuf
