#pragma once
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
namespace memory {
using ByteBuffer = std::unique_ptr<uint8_t[]>;
}
template <typename F>
struct ScopedCleanup {
  const F fn;
  explicit ScopedCleanup(F function) : fn(std::move(function)) {}
  ~ScopedCleanup() { fn(); }
};
template <typename F>
ScopedCleanup(F) -> ScopedCleanup<F>;
