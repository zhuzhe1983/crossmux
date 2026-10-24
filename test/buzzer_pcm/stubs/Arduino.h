#pragma once
#include <cstddef>
#include <cstdint>
inline uint32_t fakeMillis = 0;
inline unsigned ledcAttached = 0;
inline void (*toneHook)() = nullptr;
inline uint32_t millis() { return fakeMillis; }
inline void delay(uint32_t ms) { fakeMillis += ms; }
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) {
  ++ledcAttached;
  return true;
}
inline void ledcDetach(uint8_t) { --ledcAttached; }
inline void ledcWrite(uint8_t, uint32_t) {}
inline void ledcWriteTone(uint8_t, uint32_t) {
  if (toneHook) toneHook();
}
constexpr int OUTPUT = 1, LOW = 0;
inline void pinMode(uint8_t, int) {}
inline void digitalWrite(uint8_t, int) {}
