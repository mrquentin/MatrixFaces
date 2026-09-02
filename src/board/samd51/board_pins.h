#pragma once

#include <cstdint>

// Every pin the Matrix Portal M4 wiring depends on, in one place, so the
// shared code above never names a GPIO. The ESP32-S3 variant gets its own copy
// of this header and the composition root reads whichever is on the include
// path.
namespace board_pins {

// The Matrix Portal M4's UP/DOWN buttons; see the variant's pin table.
constexpr uint8_t kButtonUp = 2;
constexpr uint8_t kButtonDown = 3;

// RGB matrix wiring for a 128x64 panel; see
// https://learn.adafruit.com/adafruit-matrixportal-m4/protomatter-arduino-library
//
// Pin order is R1,G1,B1,R2,G2,B2 by Protomatter convention, but this panel's
// physical HUB75 wiring cycles one position off that (confirmed by testing
// pure red/green/blue: software R lit the panel's B sub-pixel, G lit R, B lit
// G). Rotated left by one position per triplet so software "R" drives the
// pin actually wired to the panel's R input, etc.
//
// Non-const because Protomatter's constructor takes uint8_t* rather than a
// pointer to const.
inline uint8_t kMatrixRgb[] = {8, 9, 7, 11, 12, 10};
inline uint8_t kMatrixAddr[] = {17, 18, 19, 20, 21};

constexpr uint8_t kMatrixClock = 14;
constexpr uint8_t kMatrixLatch = 15;
constexpr uint8_t kMatrixOe = 16;

// Panel geometry. Height is inferred by Protomatter from the address-pin
// count (5 pins -> 2 * 2^5 = 64px), so only the width is passed explicitly.
constexpr uint16_t kMatrixWidth = 128;
constexpr uint8_t kMatrixAddrPins = 5;

}  // namespace board_pins
