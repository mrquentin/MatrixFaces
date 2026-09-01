#include "flag_test_app.h"

constexpr FlagKind FlagTestApp::kSequence[];

void FlagTestApp::begin(Adafruit_Protomatter &matrix) {
  (void)matrix;
  // Forces an immediate redraw at index 0 on the next update(), same
  // reasoning as the other apps: switching back to this one should repaint
  // (and restart the cycle) right away.
  everRendered_ = false;
}

void FlagTestApp::drawCurrent(Adafruit_Protomatter &matrix) const {
  const FlagKind kind = kSequence[index_];
  const char *tla = (kind == FlagKind::kBlue) ? "ALB" : nullptr;
  drawFlag(matrix, kind, tla);
}

void FlagTestApp::update(Adafruit_Protomatter &matrix, uint32_t nowMs) {
  if (!everRendered_) {
    index_ = 0;
    lastSwitchMs_ = nowMs;
    everRendered_ = true;
    drawCurrent(matrix);
    return;
  }

  if (nowMs - lastSwitchMs_ < kHoldMs) return;

  index_ = static_cast<uint8_t>((index_ + 1) % kSequenceLen);
  lastSwitchMs_ = nowMs;
  drawCurrent(matrix);
}
