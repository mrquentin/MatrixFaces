#include "variants/registry.h"

#include <Arduino.h>
#include <IPAddress.h>

#include <cstring>

#include "apps/clock_app.h"
#include "apps/f1_flags_app.h"
#include "apps/text_app.h"
#include "board/bigbuf.h"
#include "board/net_link.h"
#include "board_caps.h"
#include "net/multiviewer_client.h"
#include "net/mv_link.h"

// The apps this firmware ships. One file, so that what is on the device is a
// property of the build rather than something scattered through main.cpp --
// and so that a build which drops an app drops its buffers with it.

namespace {

// How this board polls MultiViewer. The decision lives here, in the
// composition root, rather than inside the client: it is about what the board
// can afford, and only this file knows which board it was compiled for.
MvConfig multiViewerConfig() {
  if (board_caps::kHoldsOutboundConnections) {
    // Keep the socket open and split the query. TrackStatus and LapCount are
    // small and wanted promptly; DriverList barely changes within a session,
    // and RaceControlMessages grows all session and is what makes a response
    // large.
    //
    // Four seconds for the expensive pair rather than the two the cheap fields
    // get, so every other poll is the small one. That is a deliberate middle:
    // a blue flag arrives in RaceControlMessages, so this interval is the
    // longest one can be late, and four seconds is short enough not to matter.
    // A longer slow interval saves more traffic -- and, until phase 4.2 moves
    // polling off the render task, more of the stall that comes with it -- but
    // not enough to be worth a staler flag.
    return MvConfig{{2000, 4000, 5000, mv::kTrackStatus | mv::kLapCount,
                     mv::kDriverList | mv::kRaceControlMessages},
                    true,
                    3000};
  }

  // The M4, unchanged: everything, every two seconds, a fresh connection each
  // time, and a long backoff because a failed connect blocks it for ~10s with
  // no way to shorten that through WiFiNINA's public API. An empty slow group
  // is what makes every poll ask for everything.
  return MvConfig{{2000, 2000, 30000, mv::kAllTopics, 0}, false, 3000};
}

// Set by registerApps(). Null in a build whose F1 app was not registered,
// which is also when pollMultiViewer() has nothing to do.
MultiViewerClient *g_multiViewer = nullptr;
MvLink *g_link = nullptr;

// The MultiViewer tick. Runs on its own task on the S3 and in turn with
// everything else on the M4, and in both cases this is the only code that
// touches the client -- which is what lets the client stay unguarded.
void pollMultiViewer(uint32_t nowMs) {
  if (g_multiViewer == nullptr || g_link == nullptr) return;

  // Adopt a host the app asked for. Parsing happens here rather than in the
  // app because the socket is here; an address that will not parse means "no
  // host", the same as an empty one.
  char requested[MvLink::kHostCap];
  if (g_link->takeHostChange(requested, sizeof(requested))) {
    IPAddress ip;
    g_multiViewer->setHost(requested[0] != '\0' && ip.fromString(requested) ? ip : IPAddress());
  }

  if (!g_link->pollEnabled()) return;

  g_multiViewer->poll(nowMs);

  MvLink::Snapshot snapshot{};
  snapshot.connected = g_multiViewer->connected();
  snapshot.trackFlag = g_multiViewer->trackFlag();
  snapshot.hasLapCount = g_multiViewer->hasLapCount();
  snapshot.currentLap = g_multiViewer->currentLap();
  snapshot.totalLaps = g_multiViewer->totalLaps();
  snapshot.hasBlueFlag = g_multiViewer->hasBlueFlag();
  strncpy(snapshot.blueFlagTla, g_multiViewer->blueFlagTla(), sizeof(snapshot.blueFlagTla) - 1);
  g_link->publish(snapshot);
}

}  // namespace

void registerApps(AppRegistry &registry) {
  static ClockApp clockApp(registry.clock);
  static TextApp textApp;

  registry.scheduler.add(clockApp);
  registry.scheduler.add(textApp);

  // The F1 app exists only if the board could hand over a response buffer for
  // it. Registering it without one would give a display that says CONNECTING
  // forever with nothing to explain why, so it is better to be one app short
  // and say so on the console.
  size_t responseCap = 0;
  char *response = bigbuf::responseBuffer(responseCap);
  if (response == nullptr) {
    Serial.println(F("[apps] no response buffer; F1 flags not registered"));
    return;
  }

  static MvConfig config = multiViewerConfig();
  static MultiViewerClient multiViewer(net_link::outboundClient(), response, responseCap, config);
  static MvLink link;
  static F1FlagsApp f1FlagsApp(link);

  g_multiViewer = &multiViewer;
  g_link = &link;

  registry.scheduler.add(f1FlagsApp);
  registry.mvCounters = &multiViewer.counters();
  registry.mvPoll = &pollMultiViewer;
}
