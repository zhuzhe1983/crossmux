#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace fakeMemory {
struct Allocation {
  size_t bytes;
  std::thread::id owner;
};
inline std::mutex mutex;
inline std::map<void*, Allocation> blocks;
inline std::atomic<bool> psramEnabled{false}, failPsram{false};
inline std::atomic<unsigned> psramCalls{0};
inline std::atomic<unsigned> liveNetworkSockets{0}, jsonSocketOverlap{0};
inline std::atomic<size_t> internalFree{100000}, internalLargest{80000};
inline size_t liveBytes(bool thisThread = false) {
  std::lock_guard lock(mutex);
  size_t result = 0;
  for (const auto& item : blocks)
    if (!thisThread || item.second.owner == std::this_thread::get_id()) result += item.second.bytes;
  return result;
}
}  // namespace fakeMemory
class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes, totalBytes, minFreeBytes, largestBlockBytes;
  };
  struct PsramDeleter {
    void operator()(uint8_t* pointer) const { HalMemory::resizePsram(pointer, 0); }
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramDeleter>;
  static PsramBuffer allocatePsram(size_t bytes) {
    return PsramBuffer(static_cast<uint8_t*>(resizePsram(nullptr, bytes)));
  }
  static void* resizePsram(void* pointer, size_t bytes) {
    ++fakeMemory::psramCalls;
    if (bytes && fakeMemory::liveNetworkSockets.load()) ++fakeMemory::jsonSocketOverlap;
    std::lock_guard lock(fakeMemory::mutex);
    if (!bytes) {
      fakeMemory::blocks.erase(pointer);
      std::free(pointer);
      return nullptr;
    }
    if (!fakeMemory::psramEnabled.load() || fakeMemory::failPsram.load()) return nullptr;
    const auto previous = fakeMemory::blocks.find(pointer);
    void* updated = std::realloc(pointer, bytes);
    if (!updated) return nullptr;
    if (previous != fakeMemory::blocks.end()) fakeMemory::blocks.erase(previous);
    fakeMemory::blocks.emplace(updated, fakeMemory::Allocation{bytes, std::this_thread::get_id()});
    return updated;
  }
  static HeapStats getInternalHeap() {
    return {fakeMemory::internalFree.load(), 240000, fakeMemory::internalFree.load(),
            fakeMemory::internalLargest.load()};
  }
  static HeapStats getPsramHeap() {
    const size_t total = fakeMemory::psramEnabled.load() ? 8 * 1024 * 1024 : 0;
    return {total - fakeMemory::liveBytes(), total, total / 2, total / 2};
  }
};
