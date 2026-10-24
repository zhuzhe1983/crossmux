#pragma once
#include <chrono>
#include <cstdint>
inline uint32_t millis() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}
