#include "clock_app.h"

#include <cstdio>
#include <cstring>

ClockApp::ClockApp(TimeSource &clock)
    : clock_(clock),
      bindings_{
          SettingsBag::color("color", "Text color (0xRRGGBB)", colorRgb_),
          SettingsBag::integer("size", "Text scale (1-2)", 1, 2, textSize_),
          SettingsBag::text("tz", "POSIX time zone, e.g. CET-1CEST,M3.5.0,M10.5.0/3", tz_),
      },
      settings_(*this, bindings_, kSettingCount) {}

// Colour and size only change how the clock looks; the zone changes what it
// says, and has to reach the board before the repaint. Either way the next
// update() is forced to draw rather than skipping on an unchanged second.
void ClockApp::onSettingChanged(const char *key) {
  if (strcmp(key, "tz") == 0) clock_.setTz(tz_);
  lastRendered_ = kNeverRendered;
  lastPolledMs_ = 0;  // and don't sit out the poll interval first
}

void ClockApp::begin(Adafruit_Protomatter &matrix) {
  (void)matrix;
  // Forces update() to draw on its very next call rather than waiting for a
  // second boundary or the poll interval, so switching back to this app
  // repaints immediately.
  lastRendered_ = kNeverRendered;
  lastPolledMs_ = 0;
}

void ClockApp::update(Adafruit_Protomatter &matrix, uint32_t nowMs) {
  if (nowMs - lastPolledMs_ < kPollIntervalMs) return;
  lastPolledMs_ = nowMs;

  char text[9];  // "HH:MM:SS"
  uint32_t renderKey;

  std::tm local{};
  if (clock_.localNow(local)) {
    // Keyed on the time being *displayed*, taken from the breakdown above
    // rather than from a second look at the clock. Reading it twice let a
    // second boundary fall between the two reads: the text said second N while
    // the key recorded N+1, so the following pass compared equal and returned,
    // and the panel sat on a stale second until the one after. A key from the
    // same struct as the digits cannot disagree with them.
    //
    // The M4 got away with it -- a few microseconds of gmtime_r, and a slower
    // loop -- but it was always a race, and on the S3 it was plainly visible as
    // an irregular tick. Deriving the key also makes a timezone change repaint
    // for free, since the hour it yields moves too. Seconds-of-day tops out at
    // 86399, well clear of both sentinels.
    renderKey = static_cast<uint32_t>(local.tm_hour) * 3600U +
                static_cast<uint32_t>(local.tm_min) * 60U +
                static_cast<uint32_t>(local.tm_sec);
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

