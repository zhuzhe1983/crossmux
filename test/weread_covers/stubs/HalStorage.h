#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
inline std::vector<uint8_t> coverFile;
inline size_t storageOperations = 0;
inline bool fileExists = true;
class HalFile {
 public:
  bool opened = false;
  size_t offset = 0;
  explicit operator bool() const { return opened; }
  bool isDirectory() const { return false; }
  bool isOpen() const { return opened; }
  size_t size() const { return coverFile.size(); }
  void close() {
    opened = false;
    offset = 0;
  }
  int read(void* target, size_t count) {
    ++storageOperations;
    if (!opened || offset > coverFile.size()) return -1;
    count = std::min(count, coverFile.size() - offset);
    std::memcpy(target, coverFile.data() + offset, count);
    offset += count;
    return count;
  }
  int read() {
    uint8_t byte;
    return read(&byte, 1) == 1 ? byte : -1;
  }
  bool seek(size_t position) {
    ++storageOperations;
    if (!opened || position > coverFile.size()) return false;
    offset = position;
    return true;
  }
  bool seekCur(int64_t by) {
    if (by < 0 && uint64_t(-by) > offset) return false;
    return seek(offset + by);
  }
};
struct TestStorage {
  bool exists(const char*) {
    ++storageOperations;
    return fileExists;
  }
  bool openFileForRead(const char*, const char*, HalFile& file) {
    ++storageOperations;
    file.opened = fileExists;
    file.offset = 0;
    return fileExists;
  }
};
inline TestStorage Storage;
