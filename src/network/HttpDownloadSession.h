#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

// Independent short-transfer session for background store refreshes. It does
// not write HttpDownloader's process-global response status or route UI input.
class HttpDownloadSession {
 public:
  enum class Result : uint8_t { Ok, Cancelled, Network, StorageFailure, TooLarge, NoMemory };
  HttpDownloadSession();
  ~HttpDownloadSession();
  HttpDownloadSession(const HttpDownloadSession&) = delete;
  HttpDownloadSession& operator=(const HttpDownloadSession&) = delete;
  Result fetch(const char* url, const char* destination, size_t maximum, const std::atomic<bool>& cancel);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
