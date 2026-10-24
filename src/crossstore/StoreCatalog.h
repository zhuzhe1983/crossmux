#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Package.h"

namespace crossstore {

// A complete JSON snapshot lives on SD. Only one requested display range is
// copied into UI-owned entries; the worker never touches the Activity or UI.
class StoreCatalog {
 public:
  struct Icon {
    char artifact[256]{}, sha256[65]{};
    uint32_t bytes = 0;
  };
  struct Entry {
    Package package;
    // Already resolved against the URL of the original catalog page.
    char artifact[256]{}, sha256[65]{};
    uint32_t bytes = 0;
    Icon icon;
  };
  enum class Status : uint8_t { Empty, Cached, Refreshing, Ready, Failed };
  enum class Error : uint8_t { None, Offline, StorageFailure, Network, InvalidCatalog, Limit, NoMemory };

  StoreCatalog();
  ~StoreCatalog();
  StoreCatalog(const StoreCatalog&) = delete;
  StoreCatalog& operator=(const StoreCatalog&) = delete;

  bool configure(const char* source);
  bool load(size_t offset, size_t limit, Entry* out, size_t& count, size_t& total);
  bool lookup(const char* id, Entry& out, size_t* globalIndex = nullptr);
  bool startRefresh();
  // Non-blocking: only reaps a worker that has already released its resources.
  void poll();
  // Call before a child network activity, source change, sleep or destruction.
  // No StorageLock may be held. Worker code never requests the rendering lock;
  // ActivityManager can safely join while holding its onExit rendering lock.
  // Cancellation never forcibly kills file I/O.
  void cancelAndJoin();

  Status status() const { return status_.load(); }
  Error error() const { return error_.load(); }
  bool hasCache() const { return cached_.load(); }
  bool refreshing() const { return status() == Status::Refreshing; }
  size_t total() const { return total_.load(); }
  uint32_t revision() const { return revision_.load(); }
  uint32_t iconRevision() const { return iconRevision_.load(); }
  // Conservative count of icon references not yet confirmed in the active
  // bank. A previous-bank image can remain visible while it is still pending.
  // Zero means this generation's icon pass completed without missing images.
  size_t missingIcons() const { return missingIcons_.load(); }
  bool iconPath(const Entry& entry, char* out, size_t capacity) const;

  static constexpr size_t MAX_PAGES = 64;

 private:
  static void taskMain(void* context);
  void refresh();
  bool loadIndex();
  bool readRange(size_t offset, size_t limit, Entry* out, size_t& count, const char* id, size_t* globalIndex);
  bool parseEntry(JsonVariantConst object, const char* pageUrl, Entry& out) const;
  bool readPage(unsigned generation, size_t page, JsonDocument& document, char* url, size_t urlCapacity) const;
  void fail(Error error);

  // SD transactions are serialized by HAL. This smaller mutex protects the
  // snapshot selection/counts while a new complete generation is published.
  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
  std::atomic<bool> cancel_{false}, finished_{true}, cached_{false};
  std::atomic<Status> status_{Status::Empty};
  std::atomic<Error> error_{Error::None};
  std::atomic<size_t> total_{0}, missingIcons_{0};
  std::atomic<uint32_t> revision_{0}, iconRevision_{0};
  char source_[256]{};
  unsigned generation_ = 0;
  bool preparingGeneration_ = false;
  size_t pageCount_ = 0;
  uint8_t counts_[MAX_PAGES]{};
};
}  // namespace crossstore
