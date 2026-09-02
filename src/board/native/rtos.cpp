#include "board/rtos.h"

#include "board/command_ring.h"
#include "board/native/rtos_native.h"

// The host's copy: the same ring buffer and the same do-nothing mutex the
// SAMD51 uses, so the command queue is exercised by test/test_command_queue
// against the code path a real single-task board takes.
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

namespace rtos_native {

void reset() { rtos::g_commands.clear(); }

}  // namespace rtos_native
