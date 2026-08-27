#include "text_app.h"

#include <cstring>

namespace {

constexpr SettingDescriptor kSettings[] = {
    {"text", "Display text", SettingType::kString, 0, 0, TextApp::kTextCap - 1},
    {"size", "Text scale (1-2)", SettingType::kInt, 1, 2, 0},
    {"color", "Text color (0xRRGGBB)", SettingType::kColor, 0, 0xFFFFFF, 0},
};

}  // namespace

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

const SettingDescriptor &TextApp::settingDescriptor(uint8_t index) const {
  static constexpr SettingDescriptor kNone{"", "", SettingType::kBool, 0, 0, 0};
  return index < settingCount() ? kSettings[index] : kNone;
}

bool TextApp::getSetting(const char *key, SettingValue &out) const {
  if (strcmp(key, "text") == 0) {
    out.type = SettingType::kString;
    strncpy(out.stringValue, text_, sizeof(out.stringValue) - 1);
    out.stringValue[sizeof(out.stringValue) - 1] = '\0';
    return true;
  }
  if (strcmp(key, "size") == 0) {
    out.type = SettingType::kInt;
    out.intValue = size_;
    return true;
  }
  if (strcmp(key, "color") == 0) {
    out.type = SettingType::kColor;
    out.intValue = colorRgb_;
    return true;
  }
  return false;
}

bool TextApp::setSetting(const char *key, const SettingValue &value) {
  if (strcmp(key, "text") == 0) {
    if (value.type != SettingType::kString) return false;
    if (strlen(value.stringValue) > kTextCap - 1) return false;

    strncpy(text_, value.stringValue, sizeof(text_) - 1);
    text_[sizeof(text_) - 1] = '\0';
    needsLayout_ = true;
    return true;
  }
  if (strcmp(key, "size") == 0) {
    if (value.type != SettingType::kInt) return false;
    if (value.intValue < 1 || value.intValue > 2) return false;

    size_ = value.intValue;
    needsLayout_ = true;
    return true;
  }
  if (strcmp(key, "color") == 0) {
    if (value.type != SettingType::kColor) return false;
    if (value.intValue < 0 || value.intValue > 0xFFFFFF) return false;

    colorRgb_ = value.intValue;
    staticDrawn_ = false;  // redraw with the new color even if position is unchanged
    return true;
  }
  return false;
}
