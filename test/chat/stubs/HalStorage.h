#pragma once
#include <fcntl.h>

#include <map>
#include <string>

struct TestFile {
  std::string* data = nullptr;
  size_t position = 0;
  bool* failWrite = nullptr;
  explicit operator bool() const { return data != nullptr; }
  size_t size() const { return data ? data->size() : 0; }
  bool available() const { return data && position < data->size(); }
  int read() { return available() ? static_cast<unsigned char>((*data)[position++]) : -1; }
  size_t read(uint8_t* output, size_t length) {
    size_t count = 0;
    while (count < length && available()) output[count++] = static_cast<uint8_t>(read());
    return count;
  }
  size_t write(const uint8_t* input, size_t length) {
    if (!data || *failWrite) return 0;
    data->append(reinterpret_cast<const char*>(input), length);
    return length;
  }
  void flush() {}
};
struct TestStorage {
  std::map<std::string, std::string> files;
  bool failWrites = false;
  bool ensureDirectoryExists(const char*) { return true; }
  bool exists(const char* path) { return files.count(path); }
  TestFile open(const char* path, int flags = O_RDONLY) {
    if ((flags & O_CREAT) != 0) files.try_emplace(path);
    auto it = files.find(path);
    if (it != files.end() && (flags & O_TRUNC)) it->second.clear();
    return {it == files.end() ? nullptr : &it->second, 0, &failWrites};
  }
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to)) return false;
    auto node = files.extract(from);
    node.key() = to;
    files.insert(std::move(node));
    return true;
  }
  bool replaceFile(const char* from, const char* to) {
    if (failWrites || !exists(from)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
};
inline TestStorage Storage;
