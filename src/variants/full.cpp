#include "variants/registry.h"

#include "apps/clock_app.h"
#include "apps/f1_flags_app.h"
#include "apps/text_app.h"
#include "board/net_link.h"
#include "net/multiviewer_client.h"

// The apps this firmware ships. One file, so that what is on the device is a
// property of the build rather than something scattered through main.cpp --
// and so that a future build which drops an app drops its buffers with it.

namespace {

// By far the largest allocation in the firmware, at 17% of RAM. It sits beside
// the app that needs it rather than in main.cpp, so a build without the F1 app
// would not carry it. Phase 3.2 moves it behind board/bigbuf.h so the S3 can
// put it in PSRAM instead.
char g_multiViewerBuffer[kMvResponseCap];

}  // namespace

void registerApps(AppRegistry &registry) {
  static ClockApp clockApp(registry.clock);
  static TextApp textApp;
  static MultiViewerClient multiViewer(net_link::outboundClient(), g_multiViewerBuffer,
                                       sizeof(g_multiViewerBuffer));
  static F1FlagsApp f1FlagsApp(multiViewer);

  registry.scheduler.add(clockApp);
  registry.scheduler.add(textApp);
  registry.scheduler.add(f1FlagsApp);

  registry.mvCounters = &multiViewer.counters();
}
