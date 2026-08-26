#pragma once

#include <cstddef>
#include <cstdint>

// Raw access to one erase block of the SAMD51's internal flash, at an address
// fixed at compile time.
//
// Deliberately not using FlashStorage_SAMD's macros: those place their storage
// inside .text, so it both falls within the range bossac rewrites on upload and
// moves whenever the program's size changes. Either one destroys stored data on
// a firmware update without any warning.
namespace flash_block {

// Last 8 KB erase block of the 512 KB flash, well above the end of the image.
constexpr uint32_t kAddress = 0x0007e000;
constexpr uint32_t kBlockSize = 8192;

// True if the hardware reports the page geometry this code assumes.
bool geometryMatches();

void read(void *dest, size_t length);

// Erases the block and writes `length` bytes at its start. `length` must be a
// multiple of 4 and no larger than one page. Returns false if the data did not
// read back identical.
bool erasedWrite(const void *source, size_t length);

}  // namespace flash_block
