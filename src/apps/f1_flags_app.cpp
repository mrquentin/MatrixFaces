#include "f1_flags_app.h"

#include <IPAddress.h>

#include <cstdio>
#include <cstring>

F1FlagsApp::F1FlagsApp(MultiViewerClient &client)
    : client_(client),
      bindings_{SettingsBag::text("host", "MultiViewer host IP", host_)},
      settings_(*this, bindings_, 1) {}

void F1FlagsApp::onSettingChanged(const char *) {
  applyHost();
  everRendered_ = false;  // force a redraw; connection state just reset
}

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

void F1FlagsApp::drawSingleLine(Adafruit_Protomatter &matrix, const char *text, uint16_t bg, uint16_t fg) {
  matrix.fillScreen(bg);
  matrix.setTextSize(fitTextSize(matrix, text, kMaxTextSize, matrix.width() - 4));
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

namespace {

FlagKind toFlagKind(MultiViewerClient::Flag flag) {
  switch (flag) {
    case MultiViewerClient::Flag::kYellow:
      return FlagKind::kYellow;
    case MultiViewerClient::Flag::kSafetyCar:
      return FlagKind::kSafetyCar;
    case MultiViewerClient::Flag::kVirtualSafetyCar:
      return FlagKind::kVirtualSafetyCar;
    case MultiViewerClient::Flag::kRed:
    case MultiViewerClient::Flag::kAllClear:
    case MultiViewerClient::Flag::kUnknown:
      return FlagKind::kRed;  // unreachable for the latter two; see render()
  }
  return FlagKind::kRed;
}

}  // namespace

void F1FlagsApp::render(Adafruit_Protomatter &matrix, const RenderState &state) {
  constexpr uint16_t kBlack = 0;
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
    case Mode::kFlag: {
      const auto flag = static_cast<MultiViewerClient::Flag>(state.a);
      if (flag == MultiViewerClient::Flag::kAllClear || flag == MultiViewerClient::Flag::kUnknown) {
        // computeRenderState() never selects kFlag for these; unreachable
        // in practice, but fail safe rather than draw something misleading.
        drawSingleLine(matrix, "NO SESSION", kBlack, statusColor);
        break;
      }
      drawFlag(matrix, toFlagKind(flag), nullptr);
      break;
    }
    case Mode::kBlueFlag:
      drawFlag(matrix, FlagKind::kBlue, state.text);
      break;
    case Mode::kLapCount:
      snprintf(buf, sizeof(buf), "%lu/%lu", static_cast<unsigned long>(state.a),
               static_cast<unsigned long>(state.b));
      drawSingleLine(matrix, buf, kBlack, matrix.color565(0, 220, 90));
      break;
  }
}

