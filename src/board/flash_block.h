#pragma once

#include <cstddef>
#include <cstdint>

// Raw access to the SAMD51's internal flash, one erase block at a time, at
// addresses fixed at compile time.
//
// Deliberately not using FlashStorage_SAMD's macros: those place their storage
// inside .text, so it both falls within the range bossac rewrites on upload and
// moves whenever the program's size changes. Either one destroys stored data on
// a firmware update without any warning.
namespace flash_block {

constexpr uint32_t kBlockSize = 8192;

// Last two 8 KB erase blocks of the 512 KB flash, both well above the end of
// the image. Each persisted concern gets its own block, so writing one never
// cycles the other's erase count.
constexpr uint32_t kCredentialsAddress = 0x0007e000;
constexpr uint32_t kAppSettingsAddress = 0x0007c000;

// True if the hardware reports the page geometry this code assumes.
bool geometryMatches();

void read(uint32_t address, void *dest, size_t length);

// Erases the block at `address` and writes `length` bytes at its start.
// `length` must be a multiple of 4 and no larger than one page. Returns false
// if the data did not read back identical.
bool erasedWrite(uint32_t address, const void *source, size_t length);

}  // namespace flash_block
