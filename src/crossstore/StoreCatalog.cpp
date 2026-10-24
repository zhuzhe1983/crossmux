#include "StoreCatalog.h"

#include <CrossStoreRuntime.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>

#include "CatalogPage.h"
#include "IconBundle.h"
#include "RecoverableFile.h"
#include "network/HttpDownloadSession.h"

namespace crossstore {
namespace {
constexpr char CACHE[] = "/.crosspoint/store-cache";
constexpr char INDEX[] = "/.crosspoint/store-cache/index.json";
constexpr char INDEX_PART[] = "/.crosspoint/store-cache/index.part";
constexpr size_t ICON_BYTES = ICON_BUNDLE_BYTES;
constexpr size_t WORKER_STACK_BYTES = 8192;

// ArduinoJson owns these raw blocks through its Allocator contract. Catalog
// trees are transient but can coexist with UI reads; on S3 they must not consume
// the internal heap needed by Wi-Fi/TLS. The allocation pool is fixed per
// document, and a failed PSRAM allocation never falls back into internal RAM.
class CatalogJsonAllocator final : public ArduinoJson::Allocator {
 public:
  CatalogJsonAllocator() : psram_(HalMemory::getPsramHeap().totalBytes != 0) {}
  void* allocate(size_t bytes) override {
    void* allocated = psram_ ? HalMemory::resizePsram(nullptr, bytes) : std::malloc(bytes);
    if (!allocated && bytes) LOG_ERR("STORE", "Cannot allocate catalog JSON block (%zu bytes)", bytes);
    return allocated;
  }
  void deallocate(void* pointer) override {
    if (psram_)
      HalMemory::resizePsram(pointer, 0);
    else
      std::free(pointer);
  }
  void* reallocate(void* pointer, size_t bytes) override {
    void* allocated = psram_ ? HalMemory::resizePsram(pointer, bytes) : std::realloc(pointer, bytes);
    if (!allocated && bytes) LOG_ERR("STORE", "Cannot resize catalog JSON block (%zu bytes)", bytes);
    return allocated;
  }

 private:
  const bool psram_;
};

void logHeap(const char* phase) {
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  LOG_DBG("STORE", "%s internal=%zu largest=%zu min=%zu psram=%zu", phase, internal.freeBytes,
          internal.largestBlockBytes, internal.minFreeBytes, psram.freeBytes);
}

struct IconJobs {
  StoreCatalog::Icon entries[CATALOG_PAGE_SIZE];
};
static_assert(std::is_trivially_destructible_v<IconJobs>);

class Guard {
 public:
  explicit Guard(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(mutex_); }

 private:
  SemaphoreHandle_t mutex_;
};

bool sourceUrl(const char* url) {
  if (!url || std::strlen(url) >= 256) return false;
  const size_t prefix = !std::strncmp(url, "https://", 8) ? 8 : !std::strncmp(url, "http://", 7) ? 7 : 0;
  if (!prefix || !url[prefix] || url[prefix] == '/' || !std::strchr(url + prefix, '/')) return false;
  for (const char* p = url; *p; ++p)
    if (static_cast<unsigned char>(*p) <= 32 || *p == '\\' || *p == '@' || *p == '#') return false;
  return true;
}

bool hashText(const char* value) {
  if (std::strlen(value) != 64) return false;
  for (; *value; ++value)
    if (!(*value >= '0' && *value <= '9') && !(*value >= 'a' && *value <= 'f')) return false;
  return true;
}

void pagePath(unsigned generation, size_t page, const char* suffix, char* out, size_t size) {
  std::snprintf(out, size, "%s/%u/page-%02u.%s", CACHE, generation, static_cast<unsigned>(page), suffix);
}

bool writeText(const char* path, const char* value) {
  auto file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  const size_t bytes = std::strlen(value);
  if (!file || file.write(value, bytes) != bytes) return false;
  file.flush();
  return true;
}

bool readText(const char* path, char* out, size_t capacity) {
  auto file = Storage.open(path);
  if (!file || !file.size() || file.size() >= capacity) return false;
  const size_t bytes = file.size();
  if (file.read(out, bytes) != static_cast<int>(bytes)) return false;
  out[bytes] = 0;
  return std::strlen(out) == bytes;
}

void iconFile(unsigned generation, const char* hash, char* out, size_t capacity) {
  std::snprintf(out, capacity, "%s/%u/icons/%s.csi", CACHE, generation, hash);
}

bool copyIcon(const char* from, const char* to) {
  auto input = Storage.open(from);
  if (!input || input.size() != ICON_BYTES) return false;
  auto output = Storage.open(to, O_WRONLY | O_CREAT | O_TRUNC);
  if (!output) return false;
  uint8_t buffer[128];
  size_t remaining = ICON_BYTES;
  while (remaining) {
    const size_t count = std::min(remaining, sizeof(buffer));
    if (input.read(buffer, count) != static_cast<int>(count) || output.write(buffer, count) != count) return false;
    remaining -= count;
  }
  output.flush();
  return true;
}

bool iconHeader(const char* path) {
  auto file = Storage.open(path);
  uint8_t header[sizeof(ICON_BUNDLE_HEADER)];
  return file && file.read(header, sizeof(header)) == static_cast<int>(sizeof(header)) &&
         validIconBundleHeader(header, sizeof(header), file.size());
}

bool fetchIcon(HttpDownloadSession& http, const StoreCatalog::Icon& icon, const char* temporary,
               const std::atomic<bool>& cancel) {
  // One initial request plus one retry handles a transient connection/body
  // failure. The bounded pause is cancellable; this never runs on the UI task.
  for (unsigned attempt = 0; attempt < 2 && !cancel.load(); ++attempt) {
    const auto result = http.fetch(icon.artifact, temporary, ICON_BYTES, cancel);
    if (result == HttpDownloadSession::Result::Ok && iconHeader(temporary) &&
        verifyHash(temporary, ICON_BYTES, icon.sha256))
      return true;
    Storage.remove(temporary);
    switch (result) {
      case HttpDownloadSession::Result::Cancelled:
      case HttpDownloadSession::Result::StorageFailure:
      case HttpDownloadSession::Result::NoMemory:
      case HttpDownloadSession::Result::TooLarge:
        return false;
      case HttpDownloadSession::Result::Ok:  // Incomplete/corrupt image body.
      case HttpDownloadSession::Result::Network:
        break;
    }
    if (attempt == 0)
      for (unsigned tick = 0; tick < 5 && !cancel.load(); ++tick) vTaskDelay(pdMS_TO_TICKS(50));
  }
  return false;
}

// One scratch owner per refresh, freed before the worker signals completion.
// Heap placement avoids a >2 KiB task frame. No complete catalog is retained.
struct RefreshScratch {
  StoreCatalog::Entry entry;
  CatalogPage page;
  uint8_t counts[StoreCatalog::MAX_PAGES]{};
  uint64_t visited[StoreCatalog::MAX_PAGES]{};
  char currentUrl[256]{}, nextUrl[256]{};
  char path[160]{}, urlPath[160]{}, iconPath[160]{}, previousIcon[160]{}, temporary[168]{};
  HttpDownloadSession http;
};
}  // namespace

StoreCatalog::StoreCatalog() : mutex_(xSemaphoreCreateMutex()) {
  // One small FreeRTOS mutex for the activity lifetime; a static mutex would
  // mix separate store owners, while a stack mutex cannot outlive onEnter.
  if (!mutex_) fail(Error::NoMemory);
}

StoreCatalog::~StoreCatalog() {
  cancelAndJoin();
  if (mutex_) vSemaphoreDelete(mutex_);
}

void StoreCatalog::fail(Error error) {
  error_.store(error);
  status_.store(Status::Failed);
}

bool StoreCatalog::configure(const char* source) {
  cancelAndJoin();
  if (!mutex_) return false;
  Guard lock(mutex_);
  cached_.store(false);
  total_.store(0);
  missingIcons_.store(0);
  pageCount_ = 0;
  source_[0] = 0;
  ++revision_;
  if (!sourceUrl(source)) {
    fail(Error::InvalidCatalog);
    return false;
  }
  std::strcpy(source_, source);
  error_.store(Error::None);
  status_.store(Status::Empty);
  if (loadIndex()) {
    cached_.store(true);
    status_.store(Status::Cached);
  } else if (error_.load() == Error::NoMemory) {
    // An unreadable allocation-dependent index does not identify a safe
    // staging bank. Leave both generations intact until configure is retried.
    source_[0] = 0;
    return false;
  }
  return true;
}

bool StoreCatalog::loadIndex() {
  if (!recoverFile(Storage, INDEX)) return false;
  auto file = Storage.open(INDEX);
  if (!file || file.size() > 2048) return false;
  CatalogJsonAllocator allocator;
  JsonDocument document(&allocator);
  const auto parsed = deserializeJson(document, file, DeserializationOption::NestingLimit(3));
  if (parsed == DeserializationError::NoMemory) {
    fail(Error::NoMemory);
    return false;
  }
  if (parsed != DeserializationError::Ok || document["schema"] != 1 || !document["generation"].is<unsigned>() ||
      document["generation"].as<unsigned>() > 1 || !document["source"].is<const char*>() ||
      !document["counts"].is<JsonArrayConst>() || document["counts"].size() == 0 ||
      document["counts"].size() > MAX_PAGES)
    return false;
  // Keep the preceding generation safe even if the user has switched sources.
  generation_ = document["generation"];
  size_t sum = 0;
  for (JsonVariantConst count : document["counts"].as<JsonArrayConst>()) {
    if (!count.is<unsigned>() || count.as<unsigned>() > CATALOG_PAGE_SIZE) return false;
    sum += count.as<unsigned>();
  }
  pageCount_ = 0;
  for (JsonVariantConst count : document["counts"].as<JsonArrayConst>()) counts_[pageCount_++] = count.as<unsigned>();
  // Retain the old snapshot's page list even across a source switch: its
  // previous-bank icons must survive preparation for the new source too.
  if (std::strcmp(document["source"].as<const char*>(), source_)) return false;
  total_.store(sum);
  size_t pending = document["icons"].is<unsigned>() && document["icons"].as<unsigned>() <= sum
                       ? document["icons"].as<unsigned>()
                       : sum;  // Older indices have not recorded icon completion.
  char path[80], text[16];
  std::snprintf(path, sizeof(path), "%s/%u/icons.pending", CACHE, generation_);
  if (readText(path, text, sizeof(text))) {
    char* end = nullptr;
    const unsigned long saved = std::strtoul(text, &end, 10);
    // The terminator rejects a torn prefix (for example "1" of "12\n").
    if (*text && end && *end == '\n' && !end[1] && saved <= pending) pending = saved;
  }
  missingIcons_.store(pending);
  return true;
}

bool StoreCatalog::readPage(unsigned generation, size_t page, JsonDocument& document, char* url,
                            size_t urlCapacity) const {
  char path[96];
  pagePath(generation, page, "url", path, sizeof(path));
  if (!readText(path, url, urlCapacity) || !sourceUrl(url)) return false;
  pagePath(generation, page, "json", path, sizeof(path));
  auto file = Storage.open(path);
  return file && file.size() <= MAX_CATALOG_BYTES &&
         deserializeJson(document, file, DeserializationOption::NestingLimit(6)) == DeserializationError::Ok &&
         document["packages"].is<JsonArrayConst>() && document["packages"].size() <= CATALOG_PAGE_SIZE;
}

bool StoreCatalog::parseEntry(JsonVariantConst object, const char* pageUrl, Entry& out) const {
  out = {};
  char relative[160];
  if (!parsePackage(object, out.package, ManifestUse::Catalog) ||
      !catalogString(object["artifact"], relative, sizeof(relative)) ||
      !catalogRelativeUrl(pageUrl, relative, out.artifact, sizeof(out.artifact)) ||
      !catalogString(object["sha256"], out.sha256, sizeof(out.sha256)) || !hashText(out.sha256) ||
      !object["bytes"].is<uint32_t>())
    return false;
  out.bytes = object["bytes"];
  if (!out.bytes || out.bytes > MAX_PACKAGE_BYTES) return false;
  const auto icon = object["icon"];
  if (!icon.isNull() && catalogString(icon["artifact"], relative, sizeof(relative)) &&
      catalogRelativeUrl(pageUrl, relative, out.icon.artifact, sizeof(out.icon.artifact)) &&
      catalogString(icon["sha256"], out.icon.sha256, sizeof(out.icon.sha256)) && hashText(out.icon.sha256) &&
      icon["bytes"].is<uint32_t>() && icon["bytes"].as<uint32_t>() == ICON_BYTES)
    out.icon.bytes = ICON_BYTES;
  else
    out.icon = {};
  return true;
}

bool StoreCatalog::readRange(size_t offset, size_t limit, Entry* out, size_t& count, const char* id,
                             size_t* globalIndex) {
  count = 0;
  size_t skipped = 0;
  char url[256];
  CatalogJsonAllocator allocator;
  JsonDocument document(&allocator);
  for (size_t page = 0; page < pageCount_; ++page) {
    if (!id && skipped + counts_[page] <= offset) {
      skipped += counts_[page];
      continue;
    }
    document.clear();
    if (!readPage(generation_, page, document, url, sizeof(url))) return false;
    for (JsonVariantConst item : document["packages"].as<JsonArrayConst>()) {
      if (!parseEntry(item, url, out[count])) continue;
      if (id) {
        if (!std::strcmp(out[0].package.id, id)) {
          if (globalIndex) *globalIndex = skipped;
          count = 1;
          return true;
        }
        ++skipped;
      } else if (skipped++ >= offset && ++count == limit) {
        return true;
      }
    }
  }
  return !id;
}

bool StoreCatalog::load(size_t offset, size_t limit, Entry* out, size_t& count, size_t& total) {
  count = total = 0;
  if (!mutex_ || !out || limit == 0 || limit > CATALOG_PAGE_SIZE) return false;
  Guard lock(mutex_);
  if (!cached_.load()) return false;
  total = total_.load();
  if (offset >= total) return true;
  return readRange(offset, limit, out, count, nullptr, nullptr);
}

bool StoreCatalog::lookup(const char* id, Entry& out, size_t* globalIndex) {
  if (!mutex_ || !validId(id)) return false;
  Guard lock(mutex_);
  if (!cached_.load()) return false;
  size_t count = 0;
  return readRange(0, 1, &out, count, id, globalIndex);
}

bool StoreCatalog::iconPath(const Entry& entry, char* out, size_t capacity) const {
  if (!mutex_ || entry.icon.bytes != ICON_BYTES || !hashText(entry.icon.sha256) || capacity < 128) return false;
  Guard lock(mutex_);
  if (!cached_.load()) return false;
  iconFile(generation_, entry.icon.sha256, out, capacity);
  {
    auto file = Storage.open(out);
    if (file && file.size() == ICON_BYTES) return true;
  }
  // After publishing JSON, the previous image remains valid while its
  // identical content hash is copied into the new bank. Do not inspect that
  // bank during the next refresh's destructive preparation phase.
  if (preparingGeneration_) return false;
  iconFile(1 - generation_, entry.icon.sha256, out, capacity);
  auto previous = Storage.open(out);
  return previous && previous.size() == ICON_BYTES;
}

bool StoreCatalog::startRefresh() {
  poll();
  if (!mutex_ || task_ || !*source_) return false;
  if (WiFi.status() != WL_CONNECTED) {
    fail(Error::Offline);
    return false;
  }
  cancel_.store(false);
  finished_.store(false);
  error_.store(Error::None);
  status_.store(Status::Refreshing);
  // One bounded 8 KiB worker stack, allocated only while the store refreshes.
  // Blocking socket APIs cannot run on the UI loop without freezing input.
  if (xTaskCreate(taskMain, "store-refresh", WORKER_STACK_BYTES, this, 1, &task_) != pdPASS) {
    task_ = nullptr;
    finished_.store(true);
    fail(Error::NoMemory);
    LOG_ERR("STORE", "Cannot allocate refresh task (%zu-byte stack)", WORKER_STACK_BYTES);
    return false;
  }
  return true;
}

void StoreCatalog::poll() {
  if (task_ && finished_.load()) {
    // taskMain has released all HTTP, JSON and file resources before setting
    // finished; it performs no more owner accesses after that store.
    vTaskDelete(task_);
    task_ = nullptr;
  }
}

void StoreCatalog::cancelAndJoin() {
  if (!task_) return;
  cancel_.store(true);
  while (!finished_.load()) vTaskDelay(pdMS_TO_TICKS(10));
  poll();
}

void StoreCatalog::taskMain(void* context) {
  auto* self = static_cast<StoreCatalog*>(context);
  self->refresh();
  {
    Guard lock(self->mutex_);
    self->preparingGeneration_ = false;
  }
  if (self->cancel_.load()) {
    self->error_.store(Error::None);
    self->status_.store(self->cached_.load() ? Status::Cached : Status::Empty);
  }
  self->finished_.store(true);
  // The owner deletes this resource-free suspended task in poll()/onExit.
  vTaskSuspend(nullptr);
}

void StoreCatalog::refresh() {
  auto scratch = makeUniqueNoThrow<RefreshScratch>();
  if (!scratch) {
    LOG_ERR("STORE", "Cannot allocate refresh scratch (%zu bytes)", sizeof(RefreshScratch));
    fail(Error::NoMemory);
    return;
  }
  auto& work = *scratch;
  const unsigned previous = generation_;
  const unsigned target = 1 - previous;
  CatalogJsonAllocator allocator;
  JsonDocument document(&allocator);
  // JSON is published before optional icons finish. After cancellation, an
  // active snapshot may still borrow its only image from the other bank.
  // Preserve every such referenced image before deleting that bank. A failed
  // read/copy aborts preparation and leaves the sole old copy untouched.
  for (size_t page = 0; page < pageCount_; ++page) {
    if (cancel_.load()) return;
    document.clear();
    if (!readPage(previous, page, document, work.currentUrl, sizeof(work.currentUrl))) {
      fail(document.overflowed() ? Error::NoMemory : Error::StorageFailure);
      return;
    }
    for (JsonVariantConst item : document["packages"].as<JsonArrayConst>()) {
      if (cancel_.load()) return;
      if (!parseEntry(item, work.currentUrl, work.entry) || !work.entry.icon.bytes) continue;
      const auto& icon = work.entry.icon;
      iconFile(previous, icon.sha256, work.iconPath, sizeof(work.iconPath));
      if (iconHeader(work.iconPath) && verifyHash(work.iconPath, ICON_BYTES, icon.sha256)) continue;
      iconFile(target, icon.sha256, work.previousIcon, sizeof(work.previousIcon));
      if (!Storage.exists(work.previousIcon)) continue;
      if (!iconHeader(work.previousIcon) || !verifyHash(work.previousIcon, ICON_BYTES, icon.sha256)) {
        // Verification cannot distinguish corrupted bytes from a transient
        // card read failure. Do not erase the only candidate in either case.
        fail(Error::StorageFailure);
        return;
      }
      std::snprintf(work.path, sizeof(work.path), "%s/%u/icons", CACHE, previous);
      std::snprintf(work.temporary, sizeof(work.temporary), "%s.part", work.iconPath);
      if (!Storage.ensureDirectoryExists(work.path) || !copyIcon(work.previousIcon, work.temporary) ||
          !iconHeader(work.temporary) || !verifyHash(work.temporary, ICON_BYTES, icon.sha256) ||
          !commitFile(Storage, work.temporary, work.iconPath)) {
        Storage.remove(work.temporary);
        fail(Error::StorageFailure);
        return;
      }
      ++iconRevision_;
    }
  }
  document.clear();
  {
    Guard lock(mutex_);
    preparingGeneration_ = true;
  }
  std::snprintf(work.path, sizeof(work.path), "%s/%u", CACHE, target);
  // Only the unpublished generation is cleared. Readers hold mutex_ while
  // selecting/reading a complete generation; publication takes the same lock.
  if (!Storage.ensureDirectoryExists(CACHE) || (Storage.exists(work.path) && !Storage.removeDir(work.path)) ||
      !Storage.mkdir(work.path)) {
    fail(Error::StorageFailure);
    return;
  }
  std::strcpy(work.currentUrl, source_);
  size_t pages = 0, total = 0, iconCount = 0;
  unsigned expectedPages = 0, expectedTotal = 0;
  size_t rawTotal = 0;
  do {
    if (cancel_.load()) return;
    if (pages == MAX_PAGES) {
      fail(Error::Limit);
      return;
    }
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const char* p = work.currentUrl; *p; ++p) hash = (hash ^ static_cast<uint8_t>(*p)) * UINT64_C(1099511628211);
    for (size_t i = 0; i < pages; ++i) {
      if (work.visited[i] != hash) continue;
      pagePath(target, i, "url", work.urlPath, sizeof(work.urlPath));
      if (!readText(work.urlPath, work.nextUrl, sizeof(work.nextUrl)) || !std::strcmp(work.currentUrl, work.nextUrl)) {
        fail(Error::InvalidCatalog);
        return;
      }
    }
    work.visited[pages] = hash;
    pagePath(target, pages, "json", work.path, sizeof(work.path));
    // clear() releases ArduinoJson 7's variant and string pools; the previous
    // page must be gone before the next socket/TLS allocation begins.
    document.clear();
    logHeap("catalog JSON released");
    const auto result = work.http.fetch(work.currentUrl, work.path, MAX_CATALOG_BYTES, cancel_);
    switch (result) {
      case HttpDownloadSession::Result::Ok:
        break;
      case HttpDownloadSession::Result::Cancelled:
        return;
      case HttpDownloadSession::Result::NoMemory:
        fail(Error::NoMemory);
        return;
      case HttpDownloadSession::Result::StorageFailure:
        fail(Error::StorageFailure);
        return;
      case HttpDownloadSession::Result::TooLarge:
        fail(Error::InvalidCatalog);
        return;
      case HttpDownloadSession::Result::Network:
        fail(Error::Network);
        return;
    }
    {
      auto file = Storage.open(work.path);
      if (!file) {
        fail(Error::StorageFailure);
        return;
      }
      const auto parsed = deserializeJson(document, file, DeserializationOption::NestingLimit(6));
      if (parsed != DeserializationError::Ok) {
        fail(parsed == DeserializationError::NoMemory ? Error::NoMemory : Error::InvalidCatalog);
        return;
      }
      if (!work.page.read(document.as<JsonVariantConst>(), API_VERSION) ||
          (work.page.pages && (work.page.page != pages + 1 || work.page.pages > MAX_PAGES))) {
        fail(Error::InvalidCatalog);
        return;
      }
    }
    logHeap("catalog JSON parsed");
    if (pages == 0) {
      expectedPages = work.page.pages;
      expectedTotal = work.page.total;
    } else if (expectedPages != work.page.pages || expectedTotal != work.page.total) {
      fail(Error::InvalidCatalog);
      return;
    }
    rawTotal += document["packages"].size();
    for (JsonVariantConst item : document["packages"].as<JsonArrayConst>()) {
      if (!parseEntry(item, work.currentUrl, work.entry)) continue;
      ++work.counts[pages];
      if (work.entry.icon.bytes) ++iconCount;
    }
    total += work.counts[pages];
    pagePath(target, pages, "url", work.urlPath, sizeof(work.urlPath));
    if (!writeText(work.urlPath, work.currentUrl)) {
      fail(Error::StorageFailure);
      return;
    }
    ++pages;
    if (*work.page.next) {
      if (!catalogRelativeUrl(work.currentUrl, work.page.next, work.nextUrl, sizeof(work.nextUrl))) {
        fail(Error::InvalidCatalog);
        return;
      }
      std::strcpy(work.currentUrl, work.nextUrl);
    }
  } while (*work.page.next);
  if (cancel_.load()) return;
  if (expectedPages && (pages != expectedPages || rawTotal != expectedTotal)) {
    fail(Error::InvalidCatalog);
    return;
  }

  document.clear();
  document["schema"] = 1;
  document["source"] = source_;
  document["generation"] = target;
  document["icons"] = iconCount;
  auto counts = document["counts"].to<JsonArray>();
  for (size_t i = 0; i < pages; ++i) counts.add(work.counts[i]);
  if (document.overflowed()) {
    fail(Error::NoMemory);
    return;
  }
  {
    auto index = Storage.open(INDEX_PART, O_WRONLY | O_CREAT | O_TRUNC);
    if (!index || serializeJson(document, index) != measureJson(document)) {
      fail(Error::StorageFailure);
      return;
    }
    index.flush();
  }
  {
    Guard lock(mutex_);
    if (cancel_.load()) return;
    if (!commitFile(Storage, INDEX_PART, INDEX)) {
      fail(Error::StorageFailure);
      return;
    }
    generation_ = target;
    preparingGeneration_ = false;
    pageCount_ = pages;
    std::memcpy(counts_, work.counts, pages);
    total_.store(total);
    cached_.store(true);
    missingIcons_.store(iconCount);
    ++revision_;
  }
  document.clear();
  ScopedCleanup saveIconProgress([&] {
    // A reset or failed write leaves the index's conservative initial count.
    // One tiny state write per completed/cancelled pass, never per icon/frame.
    char text[16];
    std::snprintf(text, sizeof(text), "%u\n", static_cast<unsigned>(missingIcons_.load()));
    std::snprintf(work.path, sizeof(work.path), "%s/%u/icons.pending", CACHE, target);
    if (!writeText(work.path, text)) LOG_ERR("STORE", "Cannot save icon completion state");
    if (missingIcons_.load())
      LOG_DBG("STORE", "Catalog available; %u icon references pending", static_cast<unsigned>(missingIcons_.load()));
  });

  // Icons are optional decoration. Catalog publication is independent of icon
  // failures, so a missing image cannot remove apps or wipe a usable snapshot.
  if (!iconCount) {
    status_.store(Status::Ready);
    return;
  }
  std::snprintf(work.path, sizeof(work.path), "%s/%u/icons", CACHE, target);
  if (!Storage.mkdir(work.path)) {
    status_.store(Status::Ready);
    return;
  }
  // A maximum of sixteen fixed descriptors replaces the complete page tree
  // during image transfers. S3 keeps this ~5.2 KiB batch in PSRAM; C3 has no
  // PSRAM and uses one checked, reused heap allocation instead of a large
  // stack frame. No JSON tree survives while an image socket is active.
  HalMemory::PsramBuffer psramJobs;
  std::unique_ptr<IconJobs> internalJobs;
  IconJobs* jobs = nullptr;
  if (HalMemory::getPsramHeap().totalBytes) {
    psramJobs = HalMemory::allocatePsram(sizeof(IconJobs));
    if (psramJobs) jobs = std::construct_at(reinterpret_cast<IconJobs*>(psramJobs.get()));
  } else {
    internalJobs = makeUniqueNoThrow<IconJobs>();
    jobs = internalJobs.get();
  }
  if (!jobs) {
    LOG_ERR("STORE", "Cannot allocate icon batch (%zu bytes)", sizeof(IconJobs));
    error_.store(Error::NoMemory);
    status_.store(Status::Ready);
    return;
  }
  for (size_t page = 0; page < pages && !cancel_.load(); ++page) {
    document.clear();
    if (!readPage(target, page, document, work.currentUrl, sizeof(work.currentUrl))) continue;
    size_t jobsCount = 0;
    for (JsonVariantConst item : document["packages"].as<JsonArrayConst>()) {
      if (!parseEntry(item, work.currentUrl, work.entry) || !work.entry.icon.bytes) continue;
      jobs->entries[jobsCount++] = work.entry.icon;
    }
    document.clear();
    logHeap("icon JSON released");
    for (size_t i = 0; i < jobsCount; ++i) {
      if (cancel_.load()) return;
      const auto& icon = jobs->entries[i];
      iconFile(target, icon.sha256, work.iconPath, sizeof(work.iconPath));
      iconFile(previous, icon.sha256, work.previousIcon, sizeof(work.previousIcon));
      std::snprintf(work.temporary, sizeof(work.temporary), "%s.part", work.iconPath);
      if (iconHeader(work.iconPath) && verifyHash(work.iconPath, ICON_BYTES, icon.sha256)) {
        --missingIcons_;
        continue;
      }
      bool copied = false;
      if (iconHeader(work.previousIcon) && verifyHash(work.previousIcon, ICON_BYTES, icon.sha256))
        copied = copyIcon(work.previousIcon, work.temporary);
      if (!copied && !fetchIcon(work.http, icon, work.temporary, cancel_)) continue;
      if (cancel_.load()) return;
      if (!iconHeader(work.temporary) || !verifyHash(work.temporary, ICON_BYTES, icon.sha256) ||
          !Storage.rename(work.temporary, work.iconPath)) {
        Storage.remove(work.temporary);
        continue;
      }
      ++iconRevision_;
      --missingIcons_;
    }
  }
  status_.store(Status::Ready);
}
}  // namespace crossstore
