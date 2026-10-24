#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace radio {
// One producer (network/decoder), one consumer (audio). The caller owns the
// 64 KiB sample storage; these small atomic controls stay in internal memory.
// No locks, allocation or sample replacement. Full queues apply backpressure.
class PcmQueue {
 public:
  static constexpr size_t CAPACITY = 32768;
  static constexpr size_t PREBUFFER = 12288;  // 512 ms at 24 kHz.
  void reset(int16_t* storage);               // Only while neither task is using it.
  size_t push(const int16_t* samples, size_t count);
  size_t pop(int16_t* samples, size_t count);
  size_t available() const;  // Consumer, or approximate foreground diagnostics.

 private:
  static_assert((CAPACITY & (CAPACITY - 1)) == 0);
  // The S3 PSRAM workaround sets is_always_lock_free=false for the whole
  // atomic type (RMW may need a PSRAM lock). This queue only loads/stores aligned
  // words: the pinned Xtensa compiler emits L32I/S32I + MEMW even with
  // -mdisable-hardware-atomics. test_atomic_codegen.py verifies that contract.
  static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t));
  static_assert(alignof(std::atomic<uint32_t>) >= alignof(uint32_t));
  int16_t* samples_ = nullptr;
  std::atomic<uint32_t> read_{0}, write_{0};
};
}  // namespace radio
