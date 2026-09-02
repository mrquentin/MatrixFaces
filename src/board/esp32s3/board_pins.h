#pragma once

#include <cstdint>

// Every pin the MatrixPortal S3 wiring depends on, in one place, so the shared
// code above never names a GPIO. The SAMD51 variant is the same header with
// the M4's numbers, and the composition root reads whichever is on the include
// path.
namespace board_pins {

// The board's UP/DOWN buttons. Adafruit's variant header spells these as
// PIN_BUTTON_UP/PIN_BUTTON_DOWN; repeated as literals so this header stays
// readable next to the M4's and needs no Arduino include.
constexpr uint8_t kButtonUp = 6;
constexpr uint8_t kButtonDown = 7;

// RGB matrix wiring for a 128x64 panel. The base numbers are the MatrixPortal
// S3's HUB75 connector as Adafruit documents it -- {42,41,40,38,39,37} in
// Protomatter's R1,G1,B1,R2,G2,B2 order.
//
// They are rotated left by one position per triplet for the same reason the
// M4's are: the panel on the bench has its colour lines cycled one position
// off the standard, so software "R" has to drive the pin wired to the panel's
// R input rather than the one the connector calls R1. That was measured on the
// M4 with pure red/green/blue, and it is a property of the panel, not of the
// board -- but it is the first thing to re-check if the S3 comes up with the
// colours permuted. Unrotated, this line reads {42, 41, 40, 38, 39, 37}.
//
// Non-const because Protomatter's constructor takes uint8_t* rather than a
// pointer to const.
inline uint8_t kMatrixRgb[] = {41, 40, 42, 39, 37, 38};
inline uint8_t kMatrixAddr[] = {45, 36, 48, 35, 21};

constexpr uint8_t kMatrixClock = 2;
constexpr uint8_t kMatrixLatch = 47;
constexpr uint8_t kMatrixOe = 14;

// Panel geometry. Height is inferred by Protomatter from the address-pin
// count (5 pins -> 2 * 2^5 = 64px), so only the width is passed explicitly.
constexpr uint16_t kMatrixWidth = 128;
constexpr uint8_t kMatrixAddrPins = 5;

// Bitplanes per colour channel. Matches the M4's, but it lives here rather
// than in main.cpp because it is a refresh-timing budget as much as a colour
// choice, and the two boards refresh very differently: the S3 drives the panel
// over LCD_CAM DMA with no end-of-transfer interrupt, so Protomatter has to
// *estimate* each scanline's on-time from a measurement taken while it sets
// the transfer up.
//
// Dropping to 3 was tried during bring-up against the artefact this board
// shows when a frame is converted, and made no difference -- so the cost is
// not worth paying, and the knob is documented here rather than spent.
constexpr uint8_t kMatrixBitDepth = 4;

}  // namespace board_pins
