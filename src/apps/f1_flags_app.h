#pragma once

#include <cstddef>
#include <cstdint>

#include "app.h"
#include "flag_display.h"
#include "net/multiviewer_client.h"

// Displays F1 session state polled from a MultiViewer (https://multiviewer.app/)
// instance on the local network: connection status while connecting/
// disconnected, the current track-wide flag (large, most of the panel)
// while one is active, a driver-scoped blue flag with the driver's TLA once
// track-wide flags are clear, and otherwise the lap count.
class F1FlagsApp : public App {
 public:
  static constexpr size_t kHostCap = 32;

  // The socket the MultiViewer poll runs over. Forwarded straight to
  // MultiViewerClient; ownership stays at the composition root.
  explicit F1FlagsApp(Client &transport) : client_(transport) {}

  const char *name() const override { return "f1flags"; }
  void begin(Adafruit_Protomatter &matrix) override;
  void update(Adafruit_Protomatter &matrix, uint32_t nowMs) override;

  uint8_t settingCount() const override { return 1; }
  const SettingDescriptor &settingDescriptor(uint8_t index) const override;
  bool getSetting(const char *key, SettingValue &out) const override;
  bool setSetting(const char *key, const SettingValue &value) override;

 private:
  static constexpr uint8_t kMaxTextSize = 4;

  enum class Mode : uint8_t {
    kNotConfigured,
    kConnecting,
    kNoSession,
    kFlag,
    kBlueFlag,
    kLapCount,
  };

  // What's currently on screen, compared field-by-field against the last
  // frame so update() only repaints when something actually changed --
  // same reasoning as ClockApp/TextApp. `a`/`b` and `text` are reused across
  // modes (lap done/total; the blue-flagged driver's TLA) rather than a
  // variant, to stay allocation-free.
  struct RenderState {
    Mode mode = Mode::kNotConfigured;
    uint32_t a = 0;
    uint32_t b = 0;
    char text[MultiViewerClient::kTlaCap] = {};
  };

  void applyHost();
  RenderState computeRenderState() const;
  static void render(Adafruit_Protomatter &matrix, const RenderState &state);
  static bool renderStateEquals(const RenderState &a, const RenderState &b);
  static void drawSingleLine(Adafruit_Protomatter &matrix, const char *text, uint16_t bg, uint16_t fg);

  MultiViewerClient client_;
  char host_[kHostCap] = "";
  RenderState lastRendered_;
  bool everRendered_ = false;
};
