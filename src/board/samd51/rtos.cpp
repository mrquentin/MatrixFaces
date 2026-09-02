#include "board/rtos.h"

#include "board/command_ring.h"

// One task, so the queue is a plain ring buffer and the mutex is nothing at
// all. See board/command_ring.h for why there is no lock around the ring, and
// docs/concurrency.md for why this board stays single-threaded: phase 4.2
// splits the S3 across cores and leaves the M4 exactly as it is.
namespace rtos {
namespace {

CommandRing g_commands;

}  // namespace

bool commandPost(const Command &command) { return g_commands.post(command); }

bool commandTake(Command &out) { return g_commands.take(out); }

uint8_t commandFree() { return g_commands.free(); }

Mutex::Mutex() : handle_(nullptr) {}
Mutex::~Mutex() = default;
void Mutex::lock() {}
void Mutex::unlock() {}

}  // namespace rtos
