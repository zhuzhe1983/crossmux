#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
inline std::vector<uint8_t> fileBytes;
inline size_t storageOperations = 0, storageReadBytes = 0;
inline bool failRead = false, failSeek = false;
class HalFile {
 public:
  operator bool() const { return !fileBytes.empty(); }
  bool isDirectory() const { return false; }
  size_t size() const { return fileBytes.size(); }
  bool seek(size_t position) {
    ++storageOperations;
    offset_ = position;
    return !failSeek;
  }
  int read(void* out, size_t bytes) {
    ++storageOperations;
    if (failRead || offset_ > fileBytes.size()) return 0;
    const auto count = std::min(bytes, fileBytes.size() - offset_);
    std::memcpy(out, fileBytes.data() + offset_, count);
    offset_ += count;
    storageReadBytes += count;
    return static_cast<int>(count);
  }

 private:
  size_t offset_ = 0;
};
struct TestStorage {
  HalFile open(const char*) {
    ++storageOperations;
    return {};
  }
};
inline TestStorage Storage;
