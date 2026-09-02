#pragma once

#include <Adafruit_Protomatter.h>

#include <cstdint>

// Which flag to draw. The drawing lives apart from the app that decides when
// to draw it, so the two can be changed independently.
enum class FlagKind : uint8_t {
  kYellow,
  kSafetyCar,
  kVirtualSafetyCar,
  kRed,
  kBlue,
};

// Picks the largest text size (1..maxSize) whose rendered bounds fit within
// maxWidth x maxHeight, so a label always uses as much of its allotted space
// as legible without overflowing it. maxHeight defaults to "no constraint"
// for single-line, full-panel-width callers that don't need one.
uint8_t fitTextSize(Adafruit_Protomatter &matrix, const char *text, uint8_t maxSize, int16_t maxWidth,
                    int16_t maxHeight = INT16_MAX);

// Draws one flag full-screen, dimmed to 20% brightness (this firmware has no
// panel-wide brightness control, so every color channel is scaled down here
// instead). Real flags (Yellow/Red) are a plain colored fill -- the color
// already says everything a physical flag would, so no text is drawn on
// them. Safety Car and Virtual Safety Car have no color of their own in
// real F1 (they're conveyed by an on-screen "SC"/"VSC" board, not a
// marshal's flag), so their fill carries that label, centered, instead.
// Blue gets the same centered-label treatment using `driverTla` (which
// driver it's for) in place of a static label; pass nullptr for every
// other kind.
void drawFlag(Adafruit_Protomatter &matrix, FlagKind kind, const char *driverTla);
