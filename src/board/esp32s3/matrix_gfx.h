#pragma once

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "board_pins.h"

// This board drives the panel via ESP32-HUB75-MatrixPanel-DMA's continuous
// circular DMA instead of Adafruit_Protomatter's per-row timer ISR. That
// swap (bd matrix-faces-sjz) replaced the ISR Protomatter needed with
// hardware-timed OE/latch/row-select pacing, which has no per-row CPU
// handler left for a WiFi-driven icache stall to delay -- see
// lib/Adafruit_Protomatter/FORK.md for the artefact this eliminated and
// docs/concurrency.md for what still holds. SAMD51 is unaffected: see its
// own matrix_gfx.h, which keeps Adafruit_Protomatter permanently.
//
// A thin subclass rather than a bare alias so `matrix.show()` reads the same
// in every app regardless of board. This driver's equivalent of
// Protomatter's mandatory show() is flipDMABuffer(), which only does
// something when double_buff is set -- it is, in matrixConfig() below,
// matching the doubleBuffer=true this board's Protomatter instance ran with.
class MatrixGfx : public MatrixPanel_I2S_DMA {
 public:
  using MatrixPanel_I2S_DMA::MatrixPanel_I2S_DMA;

  void show() { flipDMABuffer(); }
};

// begin() returns bool here instead of Protomatter's ProtomatterStatus enum;
// this is the seam shared code compares against instead of naming either.
using MatrixBeginStatus = bool;
constexpr MatrixBeginStatus kMatrixBeginOk = true;

// Builds the panel config from board_pins.h. Height comes from address-pin
// count under Protomatter (2 * 2^addrPins), inferred by the driver; this
// library takes height explicitly instead, so the same arithmetic
// board_pins.h always implied is spelled out once, here.
inline HUB75_I2S_CFG matrixConfig() {
  const HUB75_I2S_CFG::i2s_pins pins = {
      static_cast<int8_t>(board_pins::kMatrixRgb[0]),
      static_cast<int8_t>(board_pins::kMatrixRgb[1]),
      static_cast<int8_t>(board_pins::kMatrixRgb[2]),
      static_cast<int8_t>(board_pins::kMatrixRgb[3]),
      static_cast<int8_t>(board_pins::kMatrixRgb[4]),
      static_cast<int8_t>(board_pins::kMatrixRgb[5]),
      static_cast<int8_t>(board_pins::kMatrixAddr[0]),
      static_cast<int8_t>(board_pins::kMatrixAddr[1]),
      static_cast<int8_t>(board_pins::kMatrixAddr[2]),
      static_cast<int8_t>(board_pins::kMatrixAddr[3]),
      static_cast<int8_t>(board_pins::kMatrixAddr[4]),
      static_cast<int8_t>(board_pins::kMatrixLatch),
      static_cast<int8_t>(board_pins::kMatrixOe),
      static_cast<int8_t>(board_pins::kMatrixClock),
  };

  const uint16_t height = static_cast<uint16_t>(2 * (1 << board_pins::kMatrixAddrPins));

  HUB75_I2S_CFG cfg(board_pins::kMatrixWidth, height, /*chain_length=*/1, pins);
  cfg.setPixelColorDepthBits(board_pins::kMatrixBitDepth);
  cfg.double_buff = true;
  return cfg;
}

// Same role main.cpp's global Protomatter instance always played, just
// behind the board seam so main.cpp never names either driver's constructor.
inline MatrixGfx &matrixInstance() {
  static MatrixGfx matrix(matrixConfig());
  return matrix;
}
