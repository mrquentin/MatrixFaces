#include "flash_block.h"

#include <Arduino.h>

#include <cstring>

namespace flash_block {
namespace {

// NVMCTRL encodes the page size as 8 << PSZ; the SAMD51 reports 6, giving 512
// bytes. An erase block is 16 pages.
uint32_t pageSize() { return 8U << NVMCTRL->PARAM.bit.PSZ; }

void waitReady() {
  while (NVMCTRL->STATUS.bit.READY == 0) {
  }
}

void waitDone() {
  while (NVMCTRL->INTFLAG.bit.DONE == 0) {
  }
  NVMCTRL->INTFLAG.reg = NVMCTRL_INTFLAG_DONE;
}

void command(uint32_t cmd) {
  waitReady();
  NVMCTRL->CTRLB.reg = static_cast<uint16_t>(NVMCTRL_CTRLB_CMDEX_KEY | cmd);
  waitDone();
}

}  // namespace

bool geometryMatches() { return pageSize() * 16U == kBlockSize; }

void read(uint32_t address, void *dest, size_t length) {
  memcpy(dest, reinterpret_cast<const void *>(address), length);
}

bool erasedWrite(uint32_t address, const void *source, size_t length) {
  if (length % 4 != 0 || length > kBlockSize) return false;

  // Manual write: the page buffer is committed explicitly (via the WP
  // command below) rather than on the last word, so a short write does not
  // commit early.
  NVMCTRL->CTRLA.bit.WMODE = 0;

  // One Erase Block covers the whole block regardless of how many of its
  // pages `length` actually touches.
  NVMCTRL->ADDR.reg = address;
  command(NVMCTRL_CTRLB_CMD_EB);

  // The page buffer only holds one page at a time, so a blob spanning
  // multiple pages needs its own clear/fill/commit cycle per page.
  const uint32_t page = pageSize();
  const auto *bytes = static_cast<const uint8_t *>(source);
  for (size_t offset = 0; offset < length; offset += page) {
    const size_t chunk = (length - offset < page) ? (length - offset) : page;
    const uint32_t pageAddress = address + static_cast<uint32_t>(offset);

    command(NVMCTRL_CTRLB_CMD_PBC);

    // The page buffer only accepts aligned 32-bit writes.
    auto *dest = reinterpret_cast<volatile uint32_t *>(pageAddress);
    for (size_t i = 0; i < chunk; i += 4) {
      uint32_t word;
      memcpy(&word, bytes + offset + i, sizeof(word));
      *dest++ = word;
    }

    NVMCTRL->ADDR.reg = pageAddress;
    command(NVMCTRL_CTRLB_CMD_WP);
  }

  return memcmp(reinterpret_cast<const void *>(address), source, length) == 0;
}

}  // namespace flash_block
