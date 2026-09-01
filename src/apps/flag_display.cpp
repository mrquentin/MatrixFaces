#include "flag_display.h"

namespace {

// This firmware has no global panel-brightness control, so "dim the flags"
// means scaling every RGB channel before it ever reaches color565().
constexpr float kBrightnessScale = 0.2f;

struct FlagStyle {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  const char *label;  // static label; nullptr for none, or for kBlue's dynamic one
  uint8_t textR;
  uint8_t textG;
  uint8_t textB;  // only meaningful when a label ends up being shown
};

FlagStyle styleFor(FlagKind kind) {
  switch (kind) {
    case FlagKind::kYellow:
      return {255, 220, 0, nullptr, 0, 0, 0};
    case FlagKind::kSafetyCar:
      return {255, 140, 0, "SC", 0, 0, 0};
    case FlagKind::kVirtualSafetyCar:
      return {255, 90, 0, "VSC", 0, 0, 0};
    case FlagKind::kRed:
      return {220, 0, 0, nullptr, 255, 255, 255};
    case FlagKind::kBlue:
      return {0, 90, 255, nullptr, 255, 255, 255};
  }
  return {0, 0, 0, nullptr, 255, 255, 255};
}

uint8_t dim(uint8_t channel) { return static_cast<uint8_t>(channel * kBrightnessScale); }

}  // namespace

uint8_t fitTextSize(Adafruit_Protomatter &matrix, const char *text, uint8_t maxSize, int16_t maxWidth,
                    int16_t maxHeight) {
  for (uint8_t size = maxSize; size >= 1; size--) {
    matrix.setTextSize(size);
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
    matrix.getTextBounds(text, 0, 0, &x, &y, &w, &h);
    if (static_cast<int16_t>(w) <= maxWidth && static_cast<int16_t>(h) <= maxHeight) return size;
  }
  return 1;
}

void drawFlag(Adafruit_Protomatter &matrix, FlagKind kind, const char *driverTla) {
  const FlagStyle style = styleFor(kind);

  // Blue has no static label of its own -- the driver it's for stands in
  // for one, drawn exactly like SC/VSC's label.
  const char *label = style.label;
  if (kind == FlagKind::kBlue && driverTla != nullptr && driverTla[0] != '\0') {
    label = driverTla;
  }

  matrix.fillScreen(matrix.color565(dim(style.r), dim(style.g), dim(style.b)));

  if (label != nullptr) {
    const int16_t panelW = matrix.width();
    const int16_t panelH = matrix.height();
    const int16_t margin = 8;

    matrix.setTextColor(matrix.color565(dim(style.textR), dim(style.textG), dim(style.textB)));
    const uint8_t size = fitTextSize(matrix, label, 6, panelW - margin, panelH - margin);
    matrix.setTextSize(size);

    int16_t lx;
    int16_t ly;
    uint16_t lw;
    uint16_t lh;
    matrix.getTextBounds(label, 0, 0, &lx, &ly, &lw, &lh);
    matrix.setCursor((panelW - static_cast<int16_t>(lw)) / 2 - lx,
                      (panelH - static_cast<int16_t>(lh)) / 2 - ly);
    matrix.print(label);
  }

  matrix.show();
}
