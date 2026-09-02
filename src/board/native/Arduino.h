#pragma once

// Minimal Arduino.h for host builds.
//
// The important part is that millis() is *controlled*, not real: delay()
// advances it instead of sleeping. That is what lets the timeout tests --
// slow-loris headers, stalled bodies, silent peers -- run in microseconds and
// deterministically, instead of needing a real four-second wall clock.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Client.h"
#include "IPAddress.h"
#include "Print.h"
#include "Stream.h"

// Header-only (C++17 inline variables) so the fakes need no build-system
// wiring; phase 2.3 relocates them into src/board/native anyway.
namespace fake_arduino {

inline uint32_t g_millis = 0;
inline uint32_t g_delayed = 0;

// Sets the clock back to zero. Call from setUp() so tests cannot leak
// simulated time into each other.
inline void resetClock() {
  g_millis = 0;
  g_delayed = 0;
}

inline void setMillis(uint32_t value) { g_millis = value; }

// Total simulated milliseconds consumed by delay() since the last reset,
// which is what a test asserts on to prove a deadline actually fired.
inline uint32_t elapsedDelay() { return g_delayed; }

}  // namespace fake_arduino

inline uint32_t millis() { return fake_arduino::g_millis; }

// Advances the simulated clock. Nothing sleeps.
inline void delay(uint32_t ms) {
  fake_arduino::g_millis += ms;
  fake_arduino::g_delayed += ms;
}

#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(string_literal))
