#pragma once

#include <cstdint>

// How the firmware's four jobs get run. The jobs themselves are the same on
// every board and live in main.cpp; what differs is whether they take turns or
// run at once, and that is the board's business.
//
// The M4 has one core and one thread of control, so it calls them in order and
// each one waits for the last -- which is what it has always done, and why a
// MultiViewer poll stalls its display. That is accepted there.
//
// The S3 has two cores. Rendering gets one to itself and everything that can
// block gets the other, so a slow poll or a slow client cannot stop the panel.
// That is the whole point of the split, and the reason the command queue and
// the ownership rules in docs/concurrency.md had to come first.
namespace exec {

// Each is called repeatedly with the current millis(). None of them may
// return only when "finished": on a board that runs them as tasks they are
// the body of a loop, and on a board that runs them in turn a tick that
// blocks holds up every other.
struct Ticks {
  // Owns the display and every app. Drains the command queue.
  void (*render)(uint32_t nowMs);
  // Accepts and serves HTTP. Blocks freely; nothing waits on it.
  void (*net)(uint32_t nowMs);
  // Polls MultiViewer. May be null when the build has no app that wants one.
  void (*mv)(uint32_t nowMs);
  // Buttons, pairing, LED, clock upkeep. Cheap and periodic.
  void (*housekeep)(uint32_t nowMs);
};

// Starts whatever this board runs concurrently, and returns. Ticks it did not
// start are left to tick() below.
//
// Called once, from setup(), after everything the ticks touch is initialised
// -- a task spawned here may run before the next line of setup() does.
void start(const Ticks &ticks);

// Runs whatever start() did not, once. Call from loop().
//
// On the S3 that is housekeeping alone; the other three are tasks by then. On
// the M4 it is all four, in order.
void tick(const Ticks &ticks, uint32_t nowMs);

}  // namespace exec
