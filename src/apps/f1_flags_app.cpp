#include "f1_flags_app.h"

#include <IPAddress.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr SettingDescriptor kSettings[] = {
    {"host", "MultiViewer host IP", SettingType::kString, 0, 0, F1FlagsApp::kHostCap - 1},
};

}  // namespace

void F1FlagsApp::begin(Adafruit_Protomatter &matrix) {
  (void)matrix;
  // Forces an immediate redraw on the next update(), same reasoning as
  // ClockApp/TextApp: switching back to this app should repaint right away.
  everRendered_ = false;
}

void F1FlagsApp::applyHost() {
  IPAddress ip;
  if (host_[0] != '\0' && ip.fromString(host_)) {
    client_.setHost(ip);
  } else {
    client_.setHost(IPAddress());
  }
}

F1FlagsApp::RenderState F1FlagsApp::computeRenderState() const {
  RenderState state;

  if (host_[0] == '\0') {
    state.mode = Mode::kNotConfigured;
    return state;
  }
  if (!client_.connected()) {
    state.mode = Mode::kConnecting;
    return state;
  }

  // Track-wide flags take priority over the driver-scoped blue flag: a blue
  // flag stays in the feed (re-issued to a lapped car) even while, say, a
  // safety car is out, but showing it then would bury the more important
  // state. kUnknown is treated like "no flag" here rather than blocking the
  // rest of the screen on a status this app doesn't recognise.
  const MultiViewerClient::Flag flag = client_.trackFlag();
  if (flag != MultiViewerClient::Flag::kAllClear && flag != MultiViewerClient::Flag::kUnknown) {
    state.mode = Mode::kFlag;
    state.a = static_cast<uint32_t>(flag);
    return state;
  }

  if (client_.hasBlueFlag()) {
    state.mode = Mode::kBlueFlag;
    strncpy(state.text, client_.blueFlagTla(), sizeof(state.text) - 1);
    state.text[sizeof(state.text) - 1] = '\0';
    return state;
  }

  if (client_.hasLapCount()) {
    state.mode = Mode::kLapCount;
    state.a = client_.currentLap();
    state.b = client_.totalLaps();
    return state;
  }

  // Session started but neither a flag, a blue flag nor lap data has
  // arrived yet (e.g. right at lights-out) -- same screen as "no session".
  state.mode = Mode::kNoSession;
  return state;
}

bool F1FlagsApp::renderStateEquals(const RenderState &a, const RenderState &b) {
  return a.mode == b.mode && a.a == b.a && a.b == b.b && strcmp(a.text, b.text) == 0;
}

void F1FlagsApp::update(Adafruit_Protomatter &matrix, uint32_t nowMs) {
  client_.poll(nowMs);

  const RenderState state = computeRenderState();
  if (everRendered_ && renderStateEquals(state, lastRendered_)) return;

  render(matrix, state);
  lastRendered_ = state;
  everRendered_ = true;
}

uint8_t F1FlagsApp::fitTextSize(Adafruit_Protomatter &matrix, const char *text, uint8_t maxSize) {
  for (uint8_t size = maxSize; size >= 1; size--) {
    matrix.setTextSize(size);
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
    matrix.getTextBounds(text, 0, 0, &x, &y, &w, &h);
    if (static_cast<int16_t>(w) <= matrix.width() - 4) return size;
  }
  return 1;
}

void F1FlagsApp::drawSingleLine(Adafruit_Protomatter &matrix, const char *text, uint16_t bg, uint16_t fg) {
  matrix.fillScreen(bg);
  matrix.setTextSize(fitTextSize(matrix, text, kMaxTextSize));
  matrix.setTextColor(fg);

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

void F1FlagsApp::drawTwoLine(Adafruit_Protomatter &matrix, const char *primary, const char *secondary,
                              uint16_t bg, uint16_t fg) {
  matrix.fillScreen(bg);
  matrix.setTextColor(fg);

  matrix.setTextSize(fitTextSize(matrix, primary, kMaxTextSize));
  int16_t px;
  int16_t py;
  uint16_t pw;
  uint16_t ph;
  matrix.getTextBounds(primary, 0, 0, &px, &py, &pw, &ph);
  matrix.setCursor((matrix.width() - static_cast<int16_t>(pw)) / 2 - px,
                    matrix.height() / 4 - static_cast<int16_t>(ph) / 2 - py);
  matrix.print(primary);

  matrix.setTextSize(fitTextSize(matrix, secondary, kMaxTextSize));
  int16_t sx;
  int16_t sy;
  uint16_t sw;
  uint16_t sh;
  matrix.getTextBounds(secondary, 0, 0, &sx, &sy, &sw, &sh);
  matrix.setCursor((matrix.width() - static_cast<int16_t>(sw)) / 2 - sx,
                    (matrix.height() * 3) / 4 - static_cast<int16_t>(sh) / 2 - sy);
  matrix.print(secondary);

  matrix.show();
}

void F1FlagsApp::render(Adafruit_Protomatter &matrix, const RenderState &state) {
  constexpr uint16_t kBlack = 0;
  constexpr uint16_t kWhite = 0xFFFF;
  const uint16_t statusColor = matrix.color565(160, 160, 160);

  char buf[16];
  switch (state.mode) {
    case Mode::kNotConfigured:
      drawSingleLine(matrix, "NO HOST", kBlack, statusColor);
      break;
    case Mode::kConnecting:
      drawSingleLine(matrix, "CONNECTING", kBlack, statusColor);
      break;
    case Mode::kNoSession:
      drawSingleLine(matrix, "NO SESSION", kBlack, statusColor);
      break;
    case Mode::kFlag:
      switch (static_cast<MultiViewerClient::Flag>(state.a)) {
        case MultiViewerClient::Flag::kYellow:
          drawSingleLine(matrix, "YELLOW", matrix.color565(255, 220, 0), kBlack);
          break;
        case MultiViewerClient::Flag::kSafetyCar:
          drawSingleLine(matrix, "SAFETY CAR", matrix.color565(255, 140, 0), kBlack);
          break;
        case MultiViewerClient::Flag::kVirtualSafetyCar:
          drawSingleLine(matrix, "VIRTUAL SC", matrix.color565(255, 90, 0), kBlack);
          break;
        case MultiViewerClient::Flag::kRed:
          drawSingleLine(matrix, "RED FLAG", matrix.color565(220, 0, 0), kWhite);
          break;
        case MultiViewerClient::Flag::kAllClear:
        case MultiViewerClient::Flag::kUnknown:
          // computeRenderState() never selects kFlag for these; unreachable
          // in practice, but fail safe rather than draw nothing.
          drawSingleLine(matrix, "NO SESSION", kBlack, statusColor);
          break;
      }
      break;
    case Mode::kBlueFlag:
      drawTwoLine(matrix, "BLUE FLAG", state.text, matrix.color565(0, 90, 255), kWhite);
      break;
    case Mode::kLapCount:
      snprintf(buf, sizeof(buf), "%lu/%lu", static_cast<unsigned long>(state.a),
               static_cast<unsigned long>(state.b));
      drawSingleLine(matrix, buf, kBlack, matrix.color565(0, 220, 90));
      break;
  }
}

const SettingDescriptor &F1FlagsApp::settingDescriptor(uint8_t index) const {
  static constexpr SettingDescriptor kNone{"", "", SettingType::kBool, 0, 0, 0};
  return index < settingCount() ? kSettings[index] : kNone;
}

bool F1FlagsApp::getSetting(const char *key, SettingValue &out) const {
  if (strcmp(key, "host") == 0) {
    out.type = SettingType::kString;
    strncpy(out.stringValue, host_, sizeof(out.stringValue) - 1);
    out.stringValue[sizeof(out.stringValue) - 1] = '\0';
    return true;
  }
  return false;
}

bool F1FlagsApp::setSetting(const char *key, const SettingValue &value) {
  if (strcmp(key, "host") == 0) {
    if (value.type != SettingType::kString) return false;

    strncpy(host_, value.stringValue, sizeof(host_) - 1);
    host_[sizeof(host_) - 1] = '\0';
    applyHost();
    everRendered_ = false;  // force a redraw; connection state just reset
    return true;
  }
  return false;
}
