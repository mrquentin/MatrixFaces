#include "board/rtos.h"

#include "board/ring.h"

// One task, so the queues are plain ring buffers and the mutex is nothing at
// all. See board/ring.h for why there is no lock around them, and
// docs/concurrency.md for why this board stays single-threaded: phase 4.2
// splits the S3 across cores and leaves the M4 exactly as it is.
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
