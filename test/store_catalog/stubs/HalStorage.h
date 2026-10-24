#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

constexpr unsigned O_RDONLY = 0, O_WRONLY = 1, O_CREAT = 2, O_TRUNC = 4;
inline std::atomic<int> failIndexRename{0};
inline std::atomic<bool> failFileWrites{false};
inline std::atomic<bool> failIconReads{false};
inline std::atomic<bool> pauseIconDirectory{false}, iconDirectoryPaused{false};
inline std::filesystem::path testStorageRoot;

class HalFile {
 public:
  HalFile() = default;
  HalFile(std::filesystem::path path, unsigned flags) : path_(std::move(path)) {
    auto mode = std::ios::binary | ((flags & O_WRONLY) ? std::ios::out : std::ios::in);
    if (flags & O_TRUNC) mode |= std::ios::trunc;
    file_.open(path_, mode);
  }
  explicit operator bool() const { return file_.is_open(); }
  size_t size() const { return file_.is_open() ? std::filesystem::file_size(path_) : 0; }
  int read(void* buffer, size_t count) {
    if (failIconReads.load() && path_.extension() == ".csi") return 0;
    file_.read(static_cast<char*>(buffer), count);
    return file_.gcount();
  }
  int read() { return file_.get(); }
  size_t readBytes(char* buffer, size_t count) { return read(buffer, count); }
  size_t write(const void* buffer, size_t count) {
    if (failFileWrites.load()) return 0;
    file_.write(static_cast<const char*>(buffer), count);
    return file_ ? count : 0;
  }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  void flush() { file_.flush(); }

 private:
  std::filesystem::path path_;
  std::fstream file_;
};

class FakeStorage {
 public:
  std::filesystem::path path(const char* value) const {
    return testStorageRoot / std::filesystem::path(value).relative_path();
  }
  bool exists(const char* value) const { return std::filesystem::exists(path(value)); }
  HalFile open(const char* value, unsigned flags = O_RDONLY) { return HalFile(path(value), flags); }
  bool mkdir(const char* value) {
    if (pauseIconDirectory.load() && std::string(value).ends_with("/icons")) {
      iconDirectoryPaused.store(true);
      while (pauseIconDirectory.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::error_code error;
    return std::filesystem::create_directories(path(value), error) || (!error && exists(value));
  }
  bool ensureDirectoryExists(const char* value) { return mkdir(value); }
  bool remove(const char* value) {
    std::error_code error;
    return std::filesystem::remove(path(value), error);
  }
  bool removeDir(const char* value) {
    std::error_code error;
    std::filesystem::remove_all(path(value), error);
    return !error;
  }
  bool rename(const char* from, const char* to) {
    if (std::string(from).ends_with("index.part") && failIndexRename.load() > 0) {
      --failIndexRename;
      return false;
    }
    std::error_code error;
    std::filesystem::rename(path(from), path(to), error);
    return !error;
  }
};
inline FakeStorage fakeStorage;
#define Storage fakeStorage
