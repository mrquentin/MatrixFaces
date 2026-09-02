#include "f1_flags_app.h"

#include <IPAddress.h>

#include <cstdio>
#include <cstring>

F1FlagsApp::F1FlagsApp(MvLink &link)
    : link_(link),
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
  // Polling only happens while this app is on screen. That was previously a
  // consequence of update() being the only caller; now that the poll has its
  // own task it has to be said out loud.
  link_.setPollEnabled(true);
}

void F1FlagsApp::end() { link_.setPollEnabled(false); }

// Handed over rather than applied: the socket belongs to the poller's task, so
// this only records what was asked for and the poller adopts it on its own
// side. Parsing the address stays there too -- rejecting a malformed one is
// the poller's business, not the display's.
void F1FlagsApp::applyHost() { link_.requestHost(host_); }

F1FlagsApp::RenderState F1FlagsApp::computeRenderState(const MvLink::Snapshot &snapshot) const {
  RenderState state;

  if (host_[0] == '\0') {
    state.mode = Mode::kNotConfigured;
    return state;
  }
  if (!snapshot.connected) {
    state.mode = Mode::kConnecting;
    return state;
  }

  // Track-wide flags take priority over the driver-scoped blue flag: a blue
  // flag stays in the feed (re-issued to a lapped car) even while, say, a
  // safety car is out, but showing it then would bury the more important
  // state. kUnknown is treated like "no flag" here rather than blocking the
  // rest of the screen on a status this app doesn't recognise.
  const mv::Flag flag = snapshot.trackFlag;
  if (flag != mv::Flag::kAllClear && flag != mv::Flag::kUnknown) {
    state.mode = Mode::kFlag;
    state.a = static_cast<uint32_t>(flag);
    return state;
  }

  if (snapshot.hasBlueFlag) {
    state.mode = Mode::kBlueFlag;
    strncpy(state.text, snapshot.blueFlagTla, sizeof(state.text) - 1);
    state.text[sizeof(state.text) - 1] = '\0';
    return state;
  }

  if (snapshot.hasLapCount) {
    state.mode = Mode::kLapCount;
    state.a = snapshot.currentLap;
    state.b = snapshot.totalLaps;
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
  (void)nowMs;
  // No poll here any more: this runs on the render task, and a poll that
  // blocked on a socket would stop the panel. The poller has its own task and
  // leaves its results in the link.
  const RenderState state = computeRenderState(link_.read());
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

FlagKind toFlagKind(mv::Flag flag) {
  switch (flag) {
    case mv::Flag::kYellow:
      return FlagKind::kYellow;
    case mv::Flag::kSafetyCar:
      return FlagKind::kSafetyCar;
    case mv::Flag::kVirtualSafetyCar:
      return FlagKind::kVirtualSafetyCar;
    case mv::Flag::kRed:
    case mv::Flag::kAllClear:
    case mv::Flag::kUnknown:
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
      const auto flag = static_cast<mv::Flag>(state.a);
      if (flag == mv::Flag::kAllClear || flag == mv::Flag::kUnknown) {
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

