#pragma once

#include <cstdint>

#include "apps/app_setting.h"

// The seam between tasks, before there are any.
//
// Today every board runs one task and this queue is drained by the same loop
// that filled it. That is the point: phase 4.2 moves rendering and networking
// onto separate cores of the S3, and the way to survive that is to have the
// ownership rules already written down and already obeyed while they are still
// trivially true. docs/concurrency.md is the contract; this is its mechanism.
//
// The rule it exists to enforce: **a Command is the only way app state is
// mutated.** Handlers validate on whichever task they run on, then post. The
// render task drains and applies. Nothing else writes to an app, a setting, or
// the LED.

// One mutation. A tagged union kept as a plain aggregate -- no default member
// initializers, as elsewhere in this codebase, because the Arduino core builds
// as C++11 where an in-class initializer stops a struct being an aggregate.
//
// 60 bytes, nearly all of it the SettingValue. Copied by value into the queue
// so the poster keeps no ownership of anything: after commandPost() returns
// there is no pointer for the consuming task to outlive.
struct Command {
  enum class Kind : uint8_t {
    kSwitchApp,     // appIndex
    kApplySetting,  // appIndex, key, value
    kSetLed,        // on
  };

  Kind kind;
  uint8_t appIndex;
  bool on;
  // Long enough for every setting key the firmware has; matches the cap
  // AppSettingsStore uses on disk, so a key that fits storage fits here.
  char key[16];
  SettingValue value;
};

// Something that happened, on its way out to whoever is listening.
//
// The mirror image of a Command and deliberately not the same thing. A command
// is an instruction that must not be lost -- a refused one is a 503 and the
// client retries. An event is a notification, and the newest one is the one
// that matters: a client that missed an intermediate colour and got the final
// one has lost nothing.
struct Event {
  enum class Kind : uint8_t {
    kSettingChanged,  // appIndex, key
    kAppSwitched,     // appIndex
  };

  Kind kind;
  uint8_t appIndex;
  char key[16];
};

namespace rtos {

// Eight is what the plan sized this at and what the traffic justifies: the
// deepest single request is a settings POST carrying every key of one app,
// and the queue is drained once per loop at thousands of hertz.
constexpr uint8_t kCommandQueueDepth = 8;

// False if the queue is full. Callers must treat that as a real failure and
// say so -- the API answers 503 -- rather than dropping the mutation silently.
bool commandPost(const Command &command);

// Takes the oldest command. False when there is nothing waiting.
//
// One at a time rather than a drain-with-callback so the caller decides how
// much to do per pass, and so the queue has no opinion about who applies what.
bool commandTake(Command &out);

// How many more commands would fit. Lets a caller that needs to post several
// atomically check first, so a request either lands in full or is refused in
// full -- half a settings update applied and a 503 returned would be worse
// than not applying it at all.
uint8_t commandFree();

// Deeper than the command queue because events are cheap and bursty: applying
// several settings at once produces one event each, and they leave in a batch.
constexpr uint8_t kEventQueueDepth = 16;

// Always succeeds. A full queue drops its OLDEST entry to make room, which is
// the opposite of what commandPost does and is the right answer for a
// notification: what a listener wants when it falls behind is the current
// state, not the start of the backlog.
//
// Returns false if something had to be dropped, so a caller that cares can
// count it -- /api/metrics does.
bool eventPost(const Event &event);

// Takes the oldest event. False when there is nothing waiting.
bool eventTake(Event &out);

// Mutual exclusion, for the state a queue cannot cover: things read by one
// task while another writes them, where the reader needs a coherent view
// rather than a mutation. Phase 4.2 uses it for the MultiViewer snapshot and
// for SettingsBag's string values.
//
// A no-op on the single-threaded boards, so shared code takes the lock
// unconditionally and the boards that do not need one pay nothing.
class Mutex {
 public:
  Mutex();
  ~Mutex();

  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;

  void lock();
  void unlock();

 private:
  void *handle_;
};

class LockGuard {
 public:
  explicit LockGuard(Mutex &mutex) : mutex_(mutex) { mutex_.lock(); }
  ~LockGuard() { mutex_.unlock(); }

  LockGuard(const LockGuard &) = delete;
  LockGuard &operator=(const LockGuard &) = delete;

 private:
  Mutex &mutex_;
};

}  // namespace rtos
