#include "clock_app.h"

#include <cstdio>
#include <cstring>

namespace {
}  // namespace

ClockApp::ClockApp(const TimeSource &clock)
    : clock_(clock),
      bindings_{
          SettingsBag::color("color", "Text color (0xRRGGBB)", colorRgb_),
          SettingsBag::integer("size", "Text scale (1-2)", 1, 2, textSize_),
      },
      settings_(*this, bindings_, 2) {}

// Both settings change how the clock looks, so either one just forces the
// next update() to repaint instead of skipping on an unchanged second.
void ClockApp::onSettingChanged(const char *) { lastRendered_ = kNeverRendered; }

void ClockApp::begin(Adafruit_Protomatter &matrix) {
  (void)matrix;
  // Forces update() to draw on its very next call rather than waiting for a
  // second boundary, so switching back to this app repaints immediately.
  lastRendered_ = kNeverRendered;
}

void ClockApp::update(Adafruit_Protomatter &matrix, uint32_t nowMs) {
  (void)nowMs;

  char text[9];  // "HH:MM:SS"
  uint32_t renderKey;

  std::tm local{};
  if (clock_.localNow(local)) {
    // Keyed on the UTC epoch: one repaint per second, and a change of offset
    // lands on the next tick rather than needing its own trigger.
    renderKey = clock_.now();
    if (renderKey == lastRendered_) return;

    snprintf(text, sizeof(text), "%02u:%02u:%02u", static_cast<unsigned>(local.tm_hour),
             static_cast<unsigned>(local.tm_min), static_cast<unsigned>(local.tm_sec));
  } else {
    renderKey = kNotSyncedRendered;
    if (renderKey == lastRendered_) return;
    strcpy(text, "--:--:--");  // NOLINT(cert-err33-c) fixed-size literal, fits kNotSyncedRendered's buffer
  }
  lastRendered_ = renderKey;

  matrix.fillScreen(0);
  matrix.setTextSize(static_cast<uint8_t>(textSize_));
  matrix.setTextColor(matrix.color565(static_cast<uint8_t>((colorRgb_ >> 16) & 0xFF),
                                       static_cast<uint8_t>((colorRgb_ >> 8) & 0xFF),
                                       static_cast<uint8_t>(colorRgb_ & 0xFF)));

  int16_t boundsX;
  int16_t boundsY;
  uint16_t boundsW;
  uint16_t boundsH;
  matrix.getTextBounds(text, 0, 0, &boundsX, &boundsY, &boundsW, &boundsH);
  matrix.setCursor((matrix.width() - static_cast<int16_t>(boundsW)) / 2 - boundsX,
                    (matrix.height() - static_cast<int16_t>(boundsH)) / 2 - boundsY);
  matrix.print(text);
  matrix.show();
}

