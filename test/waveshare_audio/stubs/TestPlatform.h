#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fixture {
inline thread_local unsigned busDepth = 0;
inline std::atomic<unsigned> mutexCreates{0}, busContentions{0}, busTakes{0}, wireCalls{0}, controlCalls{0};
inline std::vector<std::string> events;
inline bool audioPresent = true, failBegin = false, failCue = false, failStreamBegin = false;
inline bool stuckAsync = false, failWrite = false, failRailOn = false, failRailOff = false;
inline std::atomic<bool> holdWrite{false}, writeEntered{false}, releaseWrite{false};
inline std::atomic<bool> pauseLdoRead{false}, ldoReadPaused{false}, releaseLdoRead{false};
inline std::vector<int16_t> pcm;
inline std::vector<uint8_t> volumes;

template <typename Predicate>
void waitFor(Predicate ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!ready()) {
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
}

inline void control(const char* name) {
  assert(busDepth > 0);  // Every codec control operation shares the PMIC guard.
  ++controlCalls;
  events.emplace_back(name);
}
}  // namespace fixture
