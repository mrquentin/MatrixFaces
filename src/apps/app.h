#pragma once

#include <cstdint>

#include <Adafruit_Protomatter.h>

// Common interface every swappable matrix app implements. AppScheduler owns
// exactly one active app at a time and drives it every loop() iteration.
class App {
 public:
  virtual ~App() = default;

  // Short identifier used in logs and (later) the /api/app switch endpoint.
  virtual const char *name() const = 0;

  // Called once when this app becomes active. The matrix is already blanked
  // by the scheduler; use this to reset any per-app render state so the next
  // update() redraws immediately instead of waiting for its own throttling.
  virtual void begin(Adafruit_Protomatter &matrix) { (void)matrix; }

  // Called every loop() iteration while this app is active. Implementations
  // should throttle their own redraws and only call matrix.show() when the
  // frame actually changed, since loop() also serves HTTP requests.
  virtual void update(Adafruit_Protomatter &matrix, uint32_t nowMs) = 0;
};
