#include "text_app.h"

#include <cstring>

namespace {

}  // namespace

TextApp::TextApp()
    : bindings_{
          SettingsBag::text("text", "Display text", text_),
          SettingsBag::integer("size", "Text scale (1-2)", 1, 2, size_),
          SettingsBag::color("color", "Text color (0xRRGGBB)", colorRgb_),
      },
      settings_(*this, bindings_, 3) {}

void TextApp::onSettingChanged(const char *key) {
  if (strcmp(key, "color") == 0) {
    // Position is unaffected, but a static frame is only drawn once, so it
    // has to be invalidated to pick up the new colour.
    staticDrawn_ = false;
    return;
  }
  // text or size: bounds and scroll-vs-static both have to be recomputed.
  needsLayout_ = true;
}

void TextApp::begin(Adafruit_Protomatter &matrix) {
  (void)matrix;
  // Forces layout() to run on the very next update(), same reasoning as
  // ClockApp: switching back to this app should repaint immediately.
  needsLayout_ = true;
}

void TextApp::layout(Adafruit_Protomatter &matrix) {
  matrix.setTextWrap(false);  // required for text to scroll off the edge
  matrix.setTextSize(static_cast<uint8_t>(size_));

  int16_t boundsX;
  int16_t boundsY;
  uint16_t boundsW;
  uint16_t boundsH;
  matrix.getTextBounds(text_, 0, 0, &boundsX, &boundsY, &boundsW, &boundsH);

  textY_ = (matrix.height() - static_cast<int16_t>(boundsH)) / 2 - boundsY;

  scrolling_ = static_cast<int16_t>(boundsW) > matrix.width();
  if (scrolling_) {
    // Starts off the right edge, loops back there once fully off the left --
    // same technique as Adafruit_Protomatter's doublebuffer_scrolltext
    // example. `- boundsX` accounts for any left-bearing the font reports,
    // same correction the static branch below already applies.
    scrollMinX_ = -static_cast<int16_t>(boundsW) - boundsX;
    scrollX_ = matrix.width();
  } else {
    staticX_ = (matrix.width() - static_cast<int16_t>(boundsW)) / 2 - boundsX;
  }

  lastFrameMs_ = 0;  // forces an immediate redraw on the next update()
}

void TextApp::draw(Adafruit_Protomatter &matrix, int16_t x, int16_t y) {
  matrix.fillScreen(0);
  matrix.setCursor(x, y);
  matrix.setTextColor(matrix.color565(static_cast<uint8_t>((colorRgb_ >> 16) & 0xFF),
                                       static_cast<uint8_t>((colorRgb_ >> 8) & 0xFF),
                                       static_cast<uint8_t>(colorRgb_ & 0xFF)));
  matrix.print(text_);
  matrix.show();
}

void TextApp::update(Adafruit_Protomatter &matrix, uint32_t nowMs) {
  if (needsLayout_) {
    layout(matrix);
    needsLayout_ = false;
    staticDrawn_ = false;
  }

  if (!scrolling_) {
    // Redraws once per layout/color change rather than every loop()
    // iteration, so a static message never contends with the HTTP server.
    if (staticDrawn_) return;
    staticDrawn_ = true;
    draw(matrix, staticX_, textY_);
    return;
  }

  if (nowMs - lastFrameMs_ < kScrollIntervalMs) return;
  lastFrameMs_ = nowMs;

  draw(matrix, scrollX_, textY_);
  if (--scrollX_ < scrollMinX_) scrollX_ = matrix.width();
}

