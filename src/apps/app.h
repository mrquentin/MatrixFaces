#pragma once

#include <cstdint>

#include "matrix_gfx.h"

#include "app_setting.h"
#include "apps/settings_bag.h"

// Common interface every swappable matrix app implements. AppScheduler owns
// exactly one active app at a time and drives it every loop() iteration.
class App : public SettingsOwner {
 public:
  virtual ~App() = default;

  // Short identifier used in logs and the /api/app switch endpoint.
  virtual const char *name() const = 0;

  // Called once when this app becomes active. The matrix is already blanked
  // by the scheduler; use this to reset any per-app render state so the next
  // update() redraws immediately instead of waiting for its own throttling.
  virtual void begin(MatrixGfx &matrix) { (void)matrix; }

  // Called once when this app stops being active, before the next app's
  // begin(). Somewhere to drop work that only makes sense while on screen --
  // network polling, timers -- rather than paying for it in the background.
  virtual void end() {}

  // Called every loop() iteration while this app is active. Implementations
  // should throttle their own redraws and only call matrix.show() when the
  // frame actually changed, since loop() also serves HTTP requests.
  virtual void update(MatrixGfx &matrix, uint32_t nowMs) = 0;

  // Optional configuration, discoverable over /api/apps without the caller
  // needing per-app knowledge. An app with settings returns a bag binding each
  // one to the member that holds it; an app with none returns nullptr and
  // writes nothing at all.
  virtual SettingsBag *settings() { return nullptr; }

  // Reading through a const App has to route via the same override, hence the
  // cast; settings() itself never mutates anything.
  const SettingsBag *settings() const { return const_cast<App *>(this)->settings(); }

  // App also inherits SettingsOwner::onSettingChanged, called once per applied
  // key so the app can react. Validation and storage are the bag's job.
};
