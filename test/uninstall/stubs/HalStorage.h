#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

constexpr unsigned O_RDONLY = 0, O_WRONLY = 1, O_CREAT = 2, O_TRUNC = 4;
inline std::filesystem::path testStorageRoot;
inline bool storageReady = true;
inline std::set<std::string> failRemove, failWrite, failRename;
inline std::vector<std::string> storageEvents;

inline std::string virtualPath(const std::filesystem::path& path) {
  return "/" + path.lexically_relative(testStorageRoot).generic_string();
}
class HalFile {
 public:
  HalFile() = default;
  HalFile(std::filesystem::path path, unsigned flags) : path_(std::move(path)) {
    directory_ = std::filesystem::is_directory(path_);
    if (directory_) return;
    auto mode = std::ios::binary | ((flags & O_WRONLY) ? std::ios::out : std::ios::in);
    if (flags & O_TRUNC) mode |= std::ios::trunc;
    file_.open(path_, mode);
  }
  explicit operator bool() const { return directory_ || file_.is_open(); }
  bool isDirectory() const { return directory_; }
  size_t size() const { return file_.is_open() ? std::filesystem::file_size(path_) : 0; }
  size_t getName(char* out, size_t capacity) const {
    const auto name = path_.filename().string();
    if (!capacity) return 0;
    const size_t length = std::min(name.size(), capacity - 1);
    std::memcpy(out, name.data(), length);
    out[length] = 0;
    return length;
  }
  HalFile openNextFile() {
    std::filesystem::path first;
    for (const auto& entry : std::filesystem::directory_iterator(path_))
      if (first.empty() || entry.path() < first) first = entry.path();
    return first.empty() ? HalFile{} : HalFile(first, O_RDONLY);
  }
  int read(void* buffer, size_t count) {
    file_.read(static_cast<char*>(buffer), count);
    return static_cast<int>(file_.gcount());
  }
  size_t write(const void* buffer, size_t count) {
    if (failWrite.count(virtualPath(path_))) return 0;
    file_.write(static_cast<const char*>(buffer), count);
    return file_ ? count : 0;
  }
  void flush() { file_.flush(); }

 private:
  std::filesystem::path path_;
  std::fstream file_;
  bool directory_ = false;
};

class FakeStorage {
 public:
  bool ready() const { return storageReady; }
  std::filesystem::path path(const char* value) const {
    return testStorageRoot / std::filesystem::path(value).relative_path();
  }
  bool exists(const char* value) const { return std::filesystem::exists(path(value)); }
  HalFile open(const char* value, unsigned flags = O_RDONLY) { return HalFile(path(value), flags); }
  bool mkdir(const char* value) {
    std::error_code error;
    return std::filesystem::create_directories(path(value), error) || (!error && exists(value));
  }
  bool remove(const char* value) {
    if (failRemove.count(value)) return false;
    std::error_code error;
    const bool result = std::filesystem::remove(path(value), error);
    if (result) storageEvents.emplace_back("remove:" + std::string(value));
    return result && !error;
  }
  bool rmdir(const char* value) { return remove(value); }
  bool rename(const char* from, const char* to) {
    if (failRename.count(to)) return false;
    std::error_code error;
    std::filesystem::rename(path(from), path(to), error);
    if (!error) storageEvents.emplace_back("rename:" + std::string(to));
    return !error;
  }
  bool replaceFile(const char* from, const char* to) { return rename(from, to); }
};
inline FakeStorage fakeStorage;
#define Storage fakeStorage
