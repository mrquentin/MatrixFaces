#include "board/rtos.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// The board where this seam has to mean something. From phase 4.2 the network
// task posts and the render task drains, on different cores, so the queue is a
// FreeRTOS one -- which gives the cross-core memory ordering a bare ring buffer
// would not.
//
// Everything is non-blocking. A post that had to wait would put the network
// task to sleep behind the render task, which is exactly the coupling the
// queue exists to remove; a full queue is reported instead, and the API turns
// it into a 503.
namespace rtos {
namespace {

// Created on first use rather than at static-init time, because the order of
// static constructors against the FreeRTOS scheduler's startup is not
// something worth depending on.
QueueHandle_t commandQueue() {
  static QueueHandle_t queue = xQueueCreate(kCommandQueueDepth, sizeof(Command));
  return queue;
}

QueueHandle_t eventQueue() {
  static QueueHandle_t queue = xQueueCreate(kEventQueueDepth, sizeof(Event));
  return queue;
}

}  // namespace

bool commandPost(const Command &command) {
  QueueHandle_t queue = commandQueue();
  if (queue == nullptr) return false;
  return xQueueSend(queue, &command, 0) == pdTRUE;
}

bool commandTake(Command &out) {
  QueueHandle_t queue = commandQueue();
  if (queue == nullptr) return false;
  return xQueueReceive(queue, &out, 0) == pdTRUE;
}

// Drop-oldest, unlike the command queue: a listener that has fallen behind
// wants the current state, not the start of the backlog. FreeRTOS has no
// overwrite for a multi-element queue, so making room means taking one off the
// front -- which is safe here because the render task is the only poster and
// the network task the only consumer.
bool eventPost(const Event &event) {
  QueueHandle_t queue = eventQueue();
  if (queue == nullptr) return false;

  if (xQueueSend(queue, &event, 0) == pdTRUE) return true;

  Event discarded{};
  xQueueReceive(queue, &discarded, 0);
  xQueueSend(queue, &event, 0);
  return false;  // something was dropped; the caller counts it
}

bool eventTake(Event &out) {
  QueueHandle_t queue = eventQueue();
  if (queue == nullptr) return false;
  return xQueueReceive(queue, &out, 0) == pdTRUE;
}

uint8_t commandFree() {
  QueueHandle_t queue = commandQueue();
  if (queue == nullptr) return 0;
  return static_cast<uint8_t>(uxQueueSpacesAvailable(queue));
}

Mutex::Mutex() : handle_(xSemaphoreCreateMutex()) {}

Mutex::~Mutex() {
  if (handle_ != nullptr) vSemaphoreDelete(static_cast<SemaphoreHandle_t>(handle_));
}

// Blocking, unlike the queue: a mutex is only ever taken around a short copy,
// and a caller that failed to get one would have nothing sensible to do.
void Mutex::lock() {
  if (handle_ != nullptr) xSemaphoreTake(static_cast<SemaphoreHandle_t>(handle_), portMAX_DELAY);
}

void Mutex::unlock() {
  if (handle_ != nullptr) xSemaphoreGive(static_cast<SemaphoreHandle_t>(handle_));
}

}  // namespace rtos
