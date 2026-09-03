#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Internal to src/board/esp32s3: the handles exec.cpp created, so metrics can
// report how much stack each task has left. Nothing above src/board/ includes
// this -- the contract in board/exec.h has no notion of a task at all.
namespace exec {

// Null until start() has run, and on any tick the build did not spawn.
TaskHandle_t renderTask();
TaskHandle_t netTask();
TaskHandle_t mvTask();
TaskHandle_t wsTask();

}  // namespace exec
