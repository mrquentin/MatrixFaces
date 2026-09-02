#include "clock_app.h"

#include <cstdio>
#include <cstring>

namespace {
}  // namespace

ClockApp::ClockApp(const TimeSource &clock, const TimezoneOffset &tz)
    : clock_(clock),
      tz_(tz),
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

  if (clock_.isValid()) {
    const uint32_t epoch = clock_.now();
    renderKey = epoch;
    if (renderKey == lastRendered_) return;

    // int64_t so a negative offset (west of UTC) can't underflow the
    // subsequent modulo; ((x % 86400) + 86400) % 86400 then normalizes to
    // [0, 86400) regardless of the sign of the shifted epoch.
    const int64_t local = static_cast<int64_t>(epoch) + tz_.offsetSeconds();
    const uint32_t secondOfDay = static_cast<uint32_t>(((local % 86400) + 86400) % 86400);
    const unsigned hours = secondOfDay / 3600;
    const unsigned minutes = (secondOfDay / 60) % 60;
    const unsigned seconds = secondOfDay % 60;
    snprintf(text, sizeof(text), "%02u:%02u:%02u", hours, minutes, seconds);
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

