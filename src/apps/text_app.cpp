#include "text_app.h"

#include <cstring>

namespace {

constexpr SettingDescriptor kSettings[] = {
    {"text", "Display text", SettingType::kString, 0, 0, TextApp::kTextCap - 1},
    {"size", "Text scale (1-2)", SettingType::kInt, 1, 2, 0},
};

}  // namespace

void TextApp::begin(Adafruit_Protomatter &matrix) {
  (void)matrix;
  // Forces update() to draw on its very next call, same reasoning as
  // ClockApp: switching back to this app should repaint immediately.
  dirty_ = true;
}

void TextApp::update(Adafruit_Protomatter &matrix, uint32_t nowMs) {
  (void)nowMs;
  if (!dirty_) return;
  dirty_ = false;

  matrix.fillScreen(0);
  matrix.setTextSize(static_cast<uint8_t>(size_));
  matrix.setTextColor(matrix.color565(255, 140, 0));

  int16_t boundsX;
  int16_t boundsY;
  uint16_t boundsW;
  uint16_t boundsH;
  matrix.getTextBounds(text_, 0, 0, &boundsX, &boundsY, &boundsW, &boundsH);
  matrix.setCursor((matrix.width() - static_cast<int16_t>(boundsW)) / 2 - boundsX,
                    (matrix.height() - static_cast<int16_t>(boundsH)) / 2 - boundsY);
  matrix.print(text_);
  matrix.show();
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
  return false;
}

bool TextApp::setSetting(const char *key, const SettingValue &value) {
  if (strcmp(key, "text") == 0) {
    if (value.type != SettingType::kString) return false;
    if (strlen(value.stringValue) > kTextCap - 1) return false;

    strncpy(text_, value.stringValue, sizeof(text_) - 1);
    text_[sizeof(text_) - 1] = '\0';
    dirty_ = true;
    return true;
  }
  if (strcmp(key, "size") == 0) {
    if (value.type != SettingType::kInt) return false;
    if (value.intValue < 1 || value.intValue > 2) return false;

    size_ = value.intValue;
    dirty_ = true;
    return true;
  }
  return false;
}
