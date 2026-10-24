#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
namespace memory {
inline size_t allocations = 0, releases = 0, largest = 0;
inline bool psram = true, fail = false;
struct FreeDeleter {
  void operator()(uint8_t* data) const {
    if (data) ++releases;
    std::free(data);
  }
};
using ByteBuffer = std::unique_ptr<uint8_t, FreeDeleter>;
inline ByteBuffer makeInternalByteBufferNoThrow(size_t bytes) {
  if (fail) return {};
  auto* data = static_cast<uint8_t*>(std::calloc(1, bytes));
  if (data) {
    ++allocations;
    if (bytes > largest) largest = bytes;
  }
  return ByteBuffer{data};
}
inline bool psramHasHeadroom(size_t, size_t, size_t) { return psram; }
inline ByteBuffer makePsramByteBufferUninitializedNoThrow(size_t bytes) {
  return psram ? makeInternalByteBufferNoThrow(bytes) : ByteBuffer{};
}
}  // namespace memory
