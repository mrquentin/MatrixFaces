#pragma once

#include "apps/app_scheduler.h"
#include "board/time_source.h"
#include "net/multiviewer_parse.h"

// Which apps a build ships is a property of the build, not of main.cpp.
// Exactly one file under src/variants/ is compiled into any given binary --
// selected by build_src_filter -- and it is the only place that names concrete
// app types.
//
// The point is subtractive as much as additive: a variant that registers no
// F1 app also constructs no MultiViewerClient, so its 32 KB response buffer
// and the whole parser link out of the image rather than sitting there unused.
//
// Plain aggregate, no default member initializers: always construct with all
// fields, matching SettingDescriptor and HttpRequest elsewhere -- the Arduino
// core builds as C++11, where an in-class initializer stops a struct being an
// aggregate at all.
struct AppRegistry {
  AppScheduler &scheduler;

  // Dependencies the composition root owns, offered to whichever apps want
  // them. A variant is free to ignore any of these.
  //
  // Not const: the clock is read for display but also *configured* -- the
  // zone a board with a POSIX TZ needs is a user-visible setting, and the app
  // that owns that setting is the one that has to pass it on.
  TimeSource &clock;

  // Filled in by a variant that polls MultiViewer, so /api/metrics can report
  // the poll's health. Left null by variants that do not, and the metrics
  // handler simply omits the section.
  const mv::Counters *mvCounters;
};

// Registers this variant's apps, in the order they appear at /api/apps.
// The first registered app is the one shown at boot.
//
// App instances are function-local statics inside the variant: they need to
// outlive the call, and this keeps them out of the image entirely when the
// variant is not selected.
void registerApps(AppRegistry &registry);
