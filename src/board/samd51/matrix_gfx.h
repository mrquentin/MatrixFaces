#pragma once

#include <Adafruit_Protomatter.h>

#include "board_pins.h"

// The contract every board satisfies: a drawable matrix type plus a
// begin()-result comparison, so shared app code never names a driver
// directly. This board keeps Adafruit_Protomatter permanently -- see the
// ESP32-S3 copy of this header (bd matrix-faces-sjz) for the continuous-DMA
// driver under test there.
using MatrixGfx = Adafruit_Protomatter;
using MatrixBeginStatus = ProtomatterStatus;
constexpr MatrixBeginStatus kMatrixBeginOk = PROTOMATTER_OK;

// Same construction main.cpp always did, just moved behind the board seam so
// the ESP32-S3 build can swap the whole driver without main.cpp caring.
inline MatrixGfx &matrixInstance() {
  static MatrixGfx matrix(board_pins::kMatrixWidth, board_pins::kMatrixBitDepth, 1,
                           board_pins::kMatrixRgb, board_pins::kMatrixAddrPins,
                           board_pins::kMatrixAddr, board_pins::kMatrixClock,
                           board_pins::kMatrixLatch, board_pins::kMatrixOe, true);
  return matrix;
}
