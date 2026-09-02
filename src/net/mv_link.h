#pragma once

#include <atomic>
#include <cstdint>

#include "board/rtos.h"
#include "net/multiviewer_parse.h"

// Everything that crosses between the MultiViewer poller and the app that
// displays it -- in both directions, which is why this is not just the
// "snapshot store" the plan named.
//
// From phase 4.2 those two live on different tasks: the poller blocks on a
// socket wherever it likes, the app repaints at frame rate, and neither waits
// for the other. Three things pass between them and each needs its own kind of
// guard:
//
//   - the result of a poll, several fields that must be read as one consistent
//     set: a mutex,
//   - whether polling should happen at all, one bit: an atomic,
//   - a change of host, a string the app was given and the poller must adopt:
//     a mutex plus a flag, taken once.
//
// On the single-task boards the mutex is a no-op, so this compiles to the same
// work the direct calls used to do.
class MvLink {
 public:
  static constexpr size_t kHostCap = 32;

  // Just what the display needs -- not the whole SessionState. The plan
  // budgeted ~1.5 KB for a memcpy of everything; the app reads seven fields,
  // so this is 24 bytes and the mutex is held for a copy that short.
  //
  // Plain aggregate, no default member initializers, as elsewhere here.
  struct Snapshot {
    bool connected;
    mv::Flag trackFlag;
    bool hasLapCount;
    uint32_t currentLap;
    uint32_t totalLaps;
    bool hasBlueFlag;
    char blueFlagTla[mv::kTlaCap];
  };

  // --- poller side -------------------------------------------------------

  void publish(const Snapshot &snapshot);

  // True once per change. The poller adopts the host on its own task, so the
  // socket is only ever touched from there.
  bool takeHostChange(char *out, size_t cap);

  bool pollEnabled() const { return pollEnabled_.load(std::memory_order_relaxed); }

  // --- app side ----------------------------------------------------------

  Snapshot read() const;

  // Set from the app's begin()/end(), so a poll only happens while the app
  // that wants it is on screen. That is what the M4 has always done by virtue
  // of update() being the only caller, and it stays true once the poller has
  // a task of its own.
  void setPollEnabled(bool enabled) { pollEnabled_.store(enabled, std::memory_order_relaxed); }

  void requestHost(const char *host);

 private:
  mutable rtos::Mutex mutex_;

  Snapshot snapshot_{};
  char host_[kHostCap] = "";
  bool hostChanged_ = false;

  // Its own atomic rather than living under the mutex: it is read on every
  // pass of the poller's loop, and a lock there would serialise the two tasks
  // for a single bit.
  std::atomic<bool> pollEnabled_{false};
};
