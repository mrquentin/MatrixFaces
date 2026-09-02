#pragma once

// Host-only control over the command queue, so a test can start from a known
// state. Not part of the rtos contract: no board implements this.
namespace rtos_native {

// Empties the queue.
void reset();

}  // namespace rtos_native
