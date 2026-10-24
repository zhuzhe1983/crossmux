#pragma once
#include <atomic>
#include <chrono>
#include <thread>
struct FakeTask {
  std::thread thread;
  std::atomic<bool> deleted{false};
};
using TaskHandle_t = FakeTask*;
inline thread_local FakeTask* currentFakeTask = nullptr;
inline int xTaskCreate(void (*function)(void*), const char*, size_t, void* context, int, TaskHandle_t* out) {
  auto* task = new FakeTask;
  *out = task;
  task->thread = std::thread([=] {
    currentFakeTask = task;
    function(context);
  });
  return 1;
}
inline void vTaskSuspend(void*) {
  while (!currentFakeTask->deleted.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
inline void vTaskDelete(TaskHandle_t task) {
  task->deleted.store(true);
  task->thread.join();
  delete task;
}
inline void vTaskDelay(unsigned milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
