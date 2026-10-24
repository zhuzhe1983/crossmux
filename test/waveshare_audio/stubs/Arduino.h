#pragma once

#include "TestPlatform.h"

using TaskHandle_t = void*;
constexpr int INPUT_PULLUP = 2, LOW = 0, HIGH = 1;
inline void pinMode(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void delay(unsigned long) {}
