#pragma once
#include <RadioPlatform.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
namespace memory {
struct FreeDeleter {
  void operator()(uint8_t* pointer) const {
    if (!pointer) return;
    std::free(pointer);
    --controls;
  }
};
using ByteBuffer = std::unique_ptr<uint8_t, FreeDeleter>;
inline ByteBuffer makeInternalByteBufferNoThrow(size_t size) {
  if (failControl) return {};
  auto* pointer = static_cast<uint8_t*>(std::calloc(1, size));
  if (pointer) ++controls;
  return ByteBuffer(pointer);
}
}  // namespace memory
