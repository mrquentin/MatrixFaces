#pragma once

#include <atomic>
#include <cstdint>

// The 60 second window, opened by the UP button, during which POST /pair is
// accepted. It closes on the first successful pairing.
//
// The button that opens it and the request that closes it already run on
// different code paths, and from phase 4.2 they run on different cores. So the
// whole state is one atomic word rather than a flag plus a deadline: those two
// could be read half-updated, and "armed with last window's deadline" is a
// window that never closes. Zero means closed; anything else is a deadline.
class PairingWindow {
 public:
  static constexpr uint32_t kWindowMs = 60000;

  void open();
  void close();

  bool isOpen() const;
  uint32_t remainingSeconds() const;

 private:
  // Relaxed ordering throughout: this word guards nothing but itself. Readers
  // want a value that was true at some recent instant, which is all a 60
  // second window can promise anyway.
  std::atomic<uint32_t> deadlineMs_{0};
};
