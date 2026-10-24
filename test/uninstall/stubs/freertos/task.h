#pragma once
#include <cstdint>
using TaskHandle_t = void*;
inline TaskHandle_t currentTestTask = reinterpret_cast<void*>(uintptr_t{1});
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return currentTestTask; }
