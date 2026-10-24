#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <unordered_map>
inline bool psramAvailable = true;
inline size_t psramAllocated = 0, psramPeak = 0, psramLargest = 0, allocationCalls = 0;
inline std::unordered_map<void*, size_t> psramSizes;
class HalMemory {
 public:
  struct PsramDeleter {
    void operator()(uint8_t* memory) const {
      if (!memory) return;
      psramAllocated -= psramSizes.at(memory);
      psramSizes.erase(memory);
      std::free(memory);
    }
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramDeleter>;
  static PsramBuffer allocatePsram(size_t bytes) {
    ++allocationCalls;
    if (!psramAvailable) return {};
    auto* memory = static_cast<uint8_t*>(std::malloc(bytes));
    if (!memory) return {};
    psramSizes[memory] = bytes;
    psramAllocated += bytes;
    psramPeak = std::max(psramPeak, psramAllocated);
    psramLargest = std::max(psramLargest, bytes);
    return PsramBuffer(memory);
  }
};
