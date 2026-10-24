#pragma once

#include "FreeRTOS.h"

struct StaticSemaphore_t {
  std::recursive_mutex mutex;
};
using SemaphoreHandle_t = StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* storage) {
  ++fixture::mutexCreates;
  return storage;
}
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, uint32_t timeout) {
  assert(timeout == portMAX_DELAY);
  if (!semaphore->mutex.try_lock()) {
    ++fixture::busContentions;
    semaphore->mutex.lock();
  }
  ++fixture::busDepth;
  ++fixture::busTakes;
  return pdTRUE;
}
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) {
  assert(fixture::busDepth > 0);
  --fixture::busDepth;
  semaphore->mutex.unlock();
  return pdTRUE;
}
