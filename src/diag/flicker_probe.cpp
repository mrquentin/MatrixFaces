// Diagnostic sketch, not part of any shipping build. Selected by env:s3_diag,
// which compiles this file *instead of* the firmware.
//
// The question it answers: does the MatrixPortal S3's visible artefact at
// redraw come from Protomatter itself, or from something the firmware does
// around it?
//
// Rather than asking someone to watch the panel, it measures. Protomatter's
// refresh is driven by an adaptive period: once per row the row handler takes
// an `elapsed` reading, filters it into core->bitZeroPeriod, and displays
// bitplane N for `bitZeroPeriod << N`. On the ESP32-S3 that reading is not a
// measurement of anything -- there is no end-of-transfer interrupt, so
// _PM_timerGetCount() returns an *estimate* built from dmaSetupTime, sampled
// inside the row ISR between clearing the timer and starting the DMA. Anything
// that delays the ISR across that window inflates the estimate, and with four
// bitplanes the longest one then displays up to eight times too long.
//
// An inflated period means fewer frames per second. So: sample
// getFrameCount() on a fixed tick and print the rate. If the artefact is the
// refresh period stretching, the tick containing a show() will show a dip, and
// the size of the dip is the size of the problem.
//
// Build stages, so the cause can be bisected rather than guessed at:
//   -DPROBE_STAGE=0   Protomatter alone, redrawing once a second
//   -DPROBE_STAGE=1   ...plus WiFi associated and idle
//   -DPROBE_STAGE=2   ...plus a busy loop touching flash, to test whether
//                     cache pressure alone is enough to do it
//   -DPROBE_STAGE=3   like 1, but WiFi is brought up from a task pinned to
//                     core 0. esp_intr_alloc() binds an interrupt to the core
//                     that allocates it, and setup() runs on core 1 -- so by
//                     default the radio's interrupt lands on the same core as
//                     the display's. This moves it off.
//   -DPROBE_STAGE=5   core 1 belongs to the display and nothing else. Built
//                     with ARDUINO_RUNNING_CORE=0, so setup(), loop() and the
//                     radio are all on core 0; matrix.begin() is called from a
//                     task pinned to core 1 purely so the display interrupt is
//                     allocated there. Level 3 is the highest priority a C
//                     handler can have, and portENTER_CRITICAL masks exactly
//                     that -- but only on the core running it. So the point of
//                     this stage is to get every critical section off the
//                     display's core.

#include <Adafruit_Protomatter.h>
#include <Arduino.h>

#if PROBE_STAGE == 1 || PROBE_STAGE == 3 || PROBE_STAGE == 5
#include <WiFi.h>

#include "secrets.h"
#define PROBE_WIFI 1
#endif

#include "board_pins.h"

// Counters patched into this environment's copy of Protomatter (see
// tools/patch_protomatter_diag.py). `elapsed` is the per-row reading that
// feeds bitZeroPeriod; a spike is one more than 1.5x the floor the library
// computed at begin(). Reading these is the whole point of the exercise: it
// turns "I can see a flicker" into how often and how badly.
extern "C" {
extern volatile uint32_t _PM_diagRows;
extern volatile uint32_t _PM_diagSpikes;
extern volatile uint32_t _PM_diagMaxElapsed;
extern volatile uint32_t _PM_diagMaxPeriod;
extern volatile uint32_t _PM_diagMinPeriod;
extern volatile uint32_t _PM_diagClamped;
}

namespace {

Adafruit_Protomatter matrix(board_pins::kMatrixWidth, board_pins::kMatrixBitDepth, 1,
                            board_pins::kMatrixRgb, board_pins::kMatrixAddrPins,
                            board_pins::kMatrixAddr, board_pins::kMatrixClock,
                            board_pins::kMatrixLatch, board_pins::kMatrixOe, true);

constexpr uint32_t kSampleMs = 100;
constexpr uint32_t kRedrawMs = 1000;

uint32_t lastSampleMs = 0;
uint32_t lastRedrawMs = 0;
uint32_t seconds = 0;
bool redrewThisSample = false;

// Same shape of work ClockApp does: clear, one line of centred text, show.
void redraw() {
  char text[9];
  snprintf(text, sizeof(text), "%02u:%02u:%02u", static_cast<unsigned>(seconds / 3600),
           static_cast<unsigned>((seconds / 60) % 60), static_cast<unsigned>(seconds % 60));

  matrix.fillScreen(0);
  matrix.setTextSize(1);
  matrix.setTextColor(matrix.color565(0, 180, 255));

  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
  matrix.getTextBounds(text, 0, 0, &x, &y, &w, &h);
  matrix.setCursor((matrix.width() - static_cast<int16_t>(w)) / 2 - x,
                   (matrix.height() - static_cast<int16_t>(h)) / 2 - y);
  matrix.print(text);
  matrix.show();
}

#if PROBE_STAGE == 5
volatile bool displayReady = false;
ProtomatterStatus displayStatus = PROTOMATTER_ERR_ARG;

// Exists only to run begin() on core 1 and then get out of the way, leaving
// that core with the display interrupt and the idle task and nothing else.
void beginDisplay(void *unused) {
  (void)unused;
  displayStatus = matrix.begin();
  displayReady = true;
  vTaskDelete(nullptr);
}
#endif

#if defined(PROBE_WIFI)
// Takes a void* and returns void so it can be either called directly or handed
// to xTaskCreatePinnedToCore, which is the only difference between stages.
void bringUpWiFi(void *unused) {
  (void)unused;
  Serial.print(F("[probe] wifi init on core "));
  Serial.println(xPortGetCoreID());

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(SECRET_SSID, SECRET_PASS);

#if PROBE_STAGE == 3
  vTaskDelete(nullptr);
#endif
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 5000) {
  }

#if PROBE_STAGE == 5
  // begin() must run on core 1: that call is what allocates the display
  // interrupt, and esp_intr_alloc() binds it to the calling core. Everything
  // else -- this task, loop(), the radio -- stays on core 0.
  xTaskCreatePinnedToCore(beginDisplay, "display", 4096, nullptr, 2, nullptr, 1);
  while (!displayReady) delay(10);
  const ProtomatterStatus status = displayStatus;
#else
  const ProtomatterStatus status = matrix.begin();
#endif
  Serial.print(F("[probe] stage "));
  Serial.print(PROBE_STAGE);
  Serial.print(F(", protomatter status "));
  Serial.println(static_cast<int>(status));
  Serial.print(F("[probe] loop core "));
  Serial.println(xPortGetCoreID());
  if (status != PROTOMATTER_OK) {
    while (true) delay(1000);
  }

#if defined(PROBE_WIFI)
#if PROBE_STAGE == 3
  // Brought up from core 0 rather than from setup(), which runs on core 1.
  // esp_wifi_init() allocates the radio's interrupt on whichever core calls
  // it, so this is what decides whether it competes with the display's.
  xTaskCreatePinnedToCore(bringUpWiFi, "wifiinit", 4096, nullptr, 1, nullptr, 0);
#else
  bringUpWiFi(nullptr);
#endif
  while (WiFi.status() != WL_CONNECTED) delay(50);
  Serial.print(F("[probe] wifi up at "));
  Serial.println(WiFi.localIP());
  Serial.print(F("[probe] setup() is on core "));
  Serial.println(xPortGetCoreID());
#endif

  Serial.println(F("[probe] frames per 100ms tick; '*' marks the tick that redrew"));
  lastSampleMs = millis();
  lastRedrawMs = millis();
  matrix.getFrameCount();  // reset the counter
}

void loop() {
  const uint32_t nowMs = millis();

  if (nowMs - lastRedrawMs >= kRedrawMs) {
    lastRedrawMs = nowMs;
    ++seconds;
    redraw();
    redrewThisSample = true;
  }

#if PROBE_STAGE >= 2
  // Churn through a span of flash-resident code and data, to see whether cache
  // pressure on its own is enough to disturb the refresh -- no drawing, no
  // buffer conversion, just competition for the SPI bus the instruction cache
  // fills from.
  {
    volatile uint32_t sink = 0;
    for (uint32_t i = 0; i < 20000; ++i) sink += i * 3U;
    (void)sink;
  }
#endif

#if PROBE_STAGE == 5
  // The idle task on core 0 is watched by the task watchdog (core 1's is not),
  // so a loop that never blocks panics the board within seconds. Moving work
  // to core 0 means yielding to it -- a constraint the firmware will inherit
  // when phase 4.2 puts the network task there.
  delay(1);
#endif

  if (nowMs - lastSampleMs >= kSampleMs) {
    lastSampleMs = nowMs;

    const uint32_t rows = _PM_diagRows;
    const uint32_t spikes = _PM_diagSpikes;
    _PM_diagRows = 0;
    _PM_diagSpikes = 0;

    // Frames in the tick, then how many of this tick's rows were timed with a
    // reading well over the floor -- and, cumulatively, the worst reading and
    // worst resulting period against that floor.
    Serial.print(F("[probe] frames="));
    Serial.print(matrix.getFrameCount());
    Serial.print(F(" rows="));
    Serial.print(rows);
    Serial.print(F(" spikes="));
    Serial.print(spikes);
    Serial.print(F(" floor="));
    Serial.print(_PM_diagMinPeriod);
    Serial.print(F(" maxElapsed="));
    Serial.print(_PM_diagMaxElapsed);
    Serial.print(F(" maxPeriod="));
    Serial.print(_PM_diagMaxPeriod);
    Serial.print(F(" clamped="));
    Serial.print(_PM_diagClamped);
    Serial.println(redrewThisSample ? F(" *") : F(""));
    redrewThisSample = false;
  }
}
