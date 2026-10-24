#if defined(CROSSMUX_RADIO) && CROSSMUX_RADIO
#include "PcmQueue.h"

#include <algorithm>
#include <cstring>

namespace radio {
void PcmQueue::reset(int16_t* storage) {
  samples_ = storage;
  read_.store(0, std::memory_order_relaxed);
  write_.store(0, std::memory_order_relaxed);
}

size_t PcmQueue::push(const int16_t* samples, size_t count) {
  const uint32_t write = write_.load(std::memory_order_relaxed);
  const uint32_t read = read_.load(std::memory_order_acquire);
  const size_t used = static_cast<uint32_t>(write - read);
  count = std::min(count, CAPACITY - used);
  if (!count) return 0;
  const size_t offset = write & (CAPACITY - 1), first = std::min(count, CAPACITY - offset);
  std::memcpy(samples_ + offset, samples, first * sizeof(int16_t));
  std::memcpy(samples_, samples + first, (count - first) * sizeof(int16_t));
  write_.store(write + static_cast<uint32_t>(count), std::memory_order_release);
  return count;
}

size_t PcmQueue::pop(int16_t* samples, size_t count) {
  const uint32_t read = read_.load(std::memory_order_relaxed);
  const uint32_t write = write_.load(std::memory_order_acquire);
  count = std::min(count, size_t{static_cast<uint32_t>(write - read)});
  if (!count) return 0;
  const size_t offset = read & (CAPACITY - 1), first = std::min(count, CAPACITY - offset);
  std::memcpy(samples, samples_ + offset, first * sizeof(int16_t));
  std::memcpy(samples + first, samples_, (count - first) * sizeof(int16_t));
  read_.store(read + static_cast<uint32_t>(count), std::memory_order_release);
  return count;
}

size_t PcmQueue::available() const {
  const uint32_t read = read_.load(std::memory_order_acquire);
  const uint32_t write = write_.load(std::memory_order_acquire);
  // A third-party diagnostic can observe a newer write than read. Clamp its
  // approximate result; the sole consumer's read index cannot race itself.
  return std::min(CAPACITY, size_t{static_cast<uint32_t>(write - read)});
}
}  // namespace radio
#endif
