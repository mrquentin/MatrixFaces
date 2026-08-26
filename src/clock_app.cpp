#include "clock_app.h"

#include <cstdio>
#include <cstring>

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

    const unsigned hours = (epoch / 3600) % 24;
    const unsigned minutes = (epoch / 60) % 60;
    const unsigned seconds = epoch % 60;
    snprintf(text, sizeof(text), "%02u:%02u:%02u", hours, minutes, seconds);
  } else {
    renderKey = kNotSyncedRendered;
    if (renderKey == lastRendered_) return;
    strcpy(text, "--:--:--");  // NOLINT(cert-err33-c) fixed-size literal, fits kNotSyncedRendered's buffer
  }
  lastRendered_ = renderKey;

  matrix.fillScreen(0);
  matrix.setTextSize(1);
  matrix.setTextColor(matrix.color565(0, 180, 255));

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
