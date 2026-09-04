#include "board/exec.h"

#include "board/esp32s3/exec_tasks.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Three tasks, plus the Arduino loop task for housekeeping.
//
// Core 1 runs rendering and nothing else the firmware creates. MatrixGfx is
// not thread-safe, so every show() still has to come from the task that
// owns it -- previously a hard requirement, since Protomatter's row
// interrupt was allocated on whichever core called matrix.begin(); the
// continuous-DMA driver now in use (bd matrix-faces-sjz) has no such
// interrupt, so this is a self-imposed rule rather than a forced one. Core 0
// takes everything that can block: HTTP, and the MultiViewer poll. lwIP's
// TCP/IP task is already pinned there, so network work stays together and
// off the display's core.
//
// The stack sizes come from the plan and are checked rather than trusted:
// /api/metrics reports each task's high-water mark, so an undersized one shows
// up as a number long before it shows up as a crash.
namespace exec {
namespace {

// The plan budgeted 8 KB for rendering. Measured, that leaves 1592 bytes free:
// drainCommands() persists after applying, and AppSettingsStore::saveAll()
// puts a 2.3 KB record array and a 2.3 KB blob on the stack together. Drawing
// itself is cheap; saving is not. 12 KB restores a real margin, and
// /api/metrics reports the high-water mark so this stays measured.
constexpr uint32_t kRenderStack = 12288;
constexpr uint32_t kNetStack = 12288;
constexpr uint32_t kMvStack = 8192;
// Smaller than the others: it reads short frames into a buffer the hub already
// owns and serialises small JSON. Reported at /api/metrics like the rest.
constexpr uint32_t kWsStack = 6144;

constexpr UBaseType_t kRenderPriority = 2;
constexpr UBaseType_t kNetPriority = 1;
constexpr UBaseType_t kMvPriority = 1;
constexpr UBaseType_t kWsPriority = 1;

constexpr BaseType_t kRenderCore = 1;
constexpr BaseType_t kNetCore = 0;
constexpr BaseType_t kMvCore = 0;
constexpr BaseType_t kWsCore = 0;

// Every task is the same shape: call one tick forever. The delay is not
// pacing -- the ticks do their own -- it is what lets the idle task run.
// FreeRTOS only reclaims deleted tasks and feeds the watchdog from idle, and
// on core 0 the watchdog is armed (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is
// set; CPU1's is not), so a task there that never yields panics the board
// within seconds. Learned the hard way; see docs/concurrency.md.
[[noreturn]] void runTick(void *param) {
  auto *tick = reinterpret_cast<void (*)(uint32_t)>(param);
  while (true) {
    tick(millis());
    vTaskDelay(1);
  }
}

TaskHandle_t g_render = nullptr;
TaskHandle_t g_net = nullptr;
TaskHandle_t g_mv = nullptr;
TaskHandle_t g_ws = nullptr;

void spawn(void (*tick)(uint32_t), const char *name, uint32_t stack, UBaseType_t priority,
           BaseType_t core, TaskHandle_t *out) {
  if (tick == nullptr) return;
  xTaskCreatePinnedToCore(runTick, name, stack, reinterpret_cast<void *>(tick), priority, out,
                          core);
}

}  // namespace

void start(const Ticks &ticks) {
  spawn(ticks.render, "render", kRenderStack, kRenderPriority, kRenderCore, &g_render);
  spawn(ticks.net, "net", kNetStack, kNetPriority, kNetCore, &g_net);
  spawn(ticks.mv, "mv", kMvStack, kMvPriority, kMvCore, &g_mv);
  spawn(ticks.ws, "ws", kWsStack, kWsPriority, kWsCore, &g_ws);
}

// Housekeeping stays on the Arduino loop task, which runs on core 1 alongside
// rendering. It is cheap and periodic -- buttons, the LED, clock upkeep -- and
// giving it a task of its own would buy nothing.
void tick(const Ticks &ticks, uint32_t nowMs) {
  if (ticks.housekeep != nullptr) ticks.housekeep(nowMs);
}

// Named so metrics can report each one's remaining stack without holding the
// handles itself.
TaskHandle_t renderTask() { return g_render; }
TaskHandle_t netTask() { return g_net; }
TaskHandle_t mvTask() { return g_mv; }
TaskHandle_t wsTask() { return g_ws; }

}  // namespace exec
