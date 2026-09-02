#include "board/rtos.h"

#include "board/native/rtos_native.h"
#include "board/ring.h"

// The host's copy: the same ring buffers and the same do-nothing mutex the
// SAMD51 uses, so test_command_queue exercises the code path a real
// single-task board takes.
namespace rtos {
namespace {

Ring<Command, kCommandQueueDepth, false> g_commands;
Ring<Event, kEventQueueDepth, true> g_events;

}  // namespace

bool commandPost(const Command &command) { return g_commands.post(command); }

bool commandTake(Command &out) { return g_commands.take(out); }

uint8_t commandFree() { return g_commands.free(); }

bool eventPost(const Event &event) { return g_events.post(event); }

bool eventTake(Event &out) { return g_events.take(out); }

Mutex::Mutex() : handle_(nullptr) {}
Mutex::~Mutex() = default;
void Mutex::lock() {}
void Mutex::unlock() {}

}  // namespace rtos

namespace rtos_native {

void reset() {
  rtos::g_commands.clear();
  rtos::g_events.clear();
}

}  // namespace rtos_native
