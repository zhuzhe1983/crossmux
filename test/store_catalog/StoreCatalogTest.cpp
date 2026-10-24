#include <Arduino.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#include "crossstore/IconBundle.h"
#include "crossstore/Permissions.h"
#include "crossstore/StoreCatalog.h"
#include "network/HttpDownloadSession.h"
#include "network/HttpDownloader.h"

// Transport and digest primitives are stand-ins; production cache/session,
// manifest parser and transactional rename code are compiled above them.
std::string digest(const std::string& value) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (unsigned char c : value) hash = (hash ^ c) * UINT64_C(1099511628211);
  char text[65];
  std::snprintf(text, sizeof(text), "%016llx%016llx%016llx%016llx", static_cast<unsigned long long>(hash),
                static_cast<unsigned long long>(hash), static_cast<unsigned long long>(hash),
                static_cast<unsigned long long>(hash));
  return text;
}
bool tlsAvailable = true;
bool HttpDownloader::hasMemoryForTls() { return tlsAvailable; }
namespace crossstore {
uint32_t permissionBit(const char* name) {
  for (const auto& permission : PERMISSIONS)
    if (!std::strcmp(permission.name, name)) return permission.bit;
  return 0;
}
bool verifyHash(const char* path, size_t bytes, const char* expected) {
  std::ifstream file(Storage.path(path), std::ios::binary);
  if (!file) return false;
  std::string data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  return data.size() == bytes && digest(data) == expected;
}
}  // namespace crossstore

using Catalog = crossstore::StoreCatalog;
constexpr char SOURCE[] = "https://example.test/store/index.json";
constexpr char SECOND[] = "https://example.test/store/sub/page-2.json";
constexpr char ICON[] = "https://example.test/store/icons/a.csi";

std::string serialize(JsonDocument& document) {
  std::string result;
  serializeJson(document, result);
  return result;
}
void package(JsonObject object, unsigned index, const std::string& iconHash = "") {
  object["schema"] = 1;
  object["id"] = "app_" + std::to_string(index);
  object["version"] = "0.2.0";
  object["name"]["en"] = "Application " + std::to_string(index);
  object["name"]["zh"] = "应用";
  object["description"]["en"] = "An application";
  object["description"]["zh"] = "应用描述";
  object["runtime"] = "crossmux-lua54";
  object["api"] = index == 20 ? 6 : 2;
  object["entry"] = "main.lua";
  object["status"] = "ready";
  object["memory_bytes"] = 65536;
  object["requires"].to<JsonArray>();
  object["permissions"].to<JsonArray>();
  object["artifact"] = "packages/app_" + std::to_string(index) + ".csp";
  object["bytes"] = 1000;
  object["sha256"] = std::string(64, 'a');
  if (!iconHash.empty()) {
    object["icon"]["artifact"] = "icons/a.csi";
    object["icon"]["bytes"] = crossstore::ICON_BUNDLE_BYTES;
    object["icon"]["sha256"] = iconHash;
  }
}
void fixture(const std::string& icon) {
  fakeHttp::responses.clear();
  for (unsigned page = 1; page <= 2; ++page) {
    JsonDocument document;
    document["schema"] = 1;
    document["api"] = 1;
    document["page"] = page;
    document["pages"] = 2;
    document["total"] = 22;
    document["previous"] = page == 1 ? "" : "first.json";
    document["next"] = page == 1 ? "sub/page-2.json" : "";
    auto rows = document["packages"].to<JsonArray>();
    if (page == 1) {
      for (unsigned i = 0; i < 15; ++i) package(rows.add<JsonObject>(), i, i ? "" : digest(icon));
      rows.add<JsonObject>()["schema"] = 5;  // Invalid entry is skipped; valid neighbors remain.
    } else {
      for (unsigned i = 15; i < 21; ++i) package(rows.add<JsonObject>(), i);
    }
    fakeHttp::responses[page == 1 ? SOURCE : SECOND] = {serialize(document)};
  }
  fakeHttp::responses[ICON] = {icon};
}
void finish(Catalog& catalog) {
  for (unsigned i = 0; i < 1000 && catalog.refreshing(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(!catalog.refreshing());
  for (unsigned i = 0; i < 5; ++i) {
    catalog.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
void ready(Catalog& catalog) {
  assert(catalog.startRefresh());
  for (unsigned i = 0; i < 1000 && catalog.refreshing(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(catalog.status() == Catalog::Status::Ready);
  // Completion publication and the final suspended-task handoff may differ by
  // one scheduling slice. poll remains nonblocking and eventually reaps it.
  for (unsigned i = 0; i < 5; ++i) {
    catalog.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

int main(int argc, char** argv) {
  assert(argc == 3);
  fakeMemory::psramEnabled.store(!std::strcmp(argv[2], "psram"));
  testStorageRoot = std::filesystem::path(argv[1]) / "card";
  std::filesystem::create_directories(testStorageRoot);
  std::string icon(crossstore::ICON_BUNDLE_BYTES, '\0');
  std::memcpy(icon.data(), crossstore::ICON_BUNDLE_HEADER, sizeof(crossstore::ICON_BUNDLE_HEADER));
  fixture(icon);
  Catalog catalog;
  assert(catalog.configure(SOURCE));
  assert(!catalog.hasCache() && catalog.status() == Catalog::Status::Empty && fakeHttp::requests == 0);
  Catalog::Entry entries[16];
  size_t count = 99, total = 99;
  assert(!catalog.load(0, 16, entries, count, total) && count == 0 && total == 0);
  ready(catalog);
  assert(catalog.hasCache() && catalog.total() == 21 && fakeHttp::timeout == 3000);
  assert(catalog.missingIcons() == 0);
  assert(fakeHttp::liveSockets == 0 && fakeMemory::liveBytes() == 0 && fakeMemory::jsonSocketOverlap == 0);
  // Catalog requests own no parsed JSON; image requests own only the fixed
  // descriptor batch after releasing the JSON page that supplied it.
  assert(fakeHttp::workerMemoryAtRequest.size() == 3);
  assert(fakeHttp::workerMemoryAtRequest[0] == 0 && fakeHttp::workerMemoryAtRequest[1] == 0);
  assert(fakeHttp::workerMemoryAtRequest[2] == (fakeMemory::psramEnabled ? 16 * sizeof(Catalog::Icon) : 0));
  assert(catalog.load(14, 4, entries, count, total) && count == 4 && total == 21);
  assert(!std::strcmp(entries[0].package.id, "app_14") && !std::strcmp(entries[3].package.id, "app_17"));
  assert(!std::strcmp(entries[1].artifact, "https://example.test/store/sub/packages/app_15.csp"));
  size_t index = 999;
  assert(catalog.lookup("app_20", entries[0], &index) && index == 20 && entries[0].package.api == 6);
  assert(!catalog.lookup("missing", entries[0]) && !catalog.load(0, 17, entries, count, total));
  assert(catalog.load(21, 16, entries, count, total) && count == 0 && total == 21);
  assert(catalog.lookup("app_0", entries[0]));
  char path[160];
  assert(catalog.iconPath(entries[0], path, sizeof(path)) && catalog.iconRevision() == 1);
  // A torn completion-state write must not report that missing images are
  // complete. The index retains the conservative initial reference count.
  const auto progressPath = Storage.path(path).parent_path().parent_path() / "icons.pending";
  {
    std::ofstream(progressPath) << "0";
  }
  {
    Catalog reopened;
    assert(reopened.configure(SOURCE) && reopened.missingIcons() == 1);
  }
  {
    std::ofstream(progressPath) << "0\n";
  }

  // A JSON update can be visible before icon copying finishes. Keep the old
  // verified same-hash image available throughout this handoff.
  const std::string previousIcon = path;
  pauseIconDirectory.store(true);
  assert(catalog.startRefresh());
  while (!iconDirectoryPaused.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(catalog.lookup("app_0", entries[0]));
  assert(catalog.iconPath(entries[0], path, sizeof(path)) && previousIcon == path);
  pauseIconDirectory.store(false);
  for (unsigned i = 0; i < 1000 && catalog.refreshing(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(catalog.status() == Catalog::Status::Ready);
  catalog.cancelAndJoin();
  assert(catalog.iconPath(entries[0], path, sizeof(path)) && previousIcon != path);

  // Cancel after new JSON publication but before old icons are copied. The
  // next refresh must preserve its only old-bank image before clearing it.
  const std::string soleOldIcon = path;
  iconDirectoryPaused.store(false);
  pauseIconDirectory.store(true);
  assert(catalog.startRefresh());
  while (!iconDirectoryPaused.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(catalog.missingIcons() == 1 && catalog.iconPath(entries[0], path, sizeof(path)) && soleOldIcon == path);
  std::thread cancelling([&] { catalog.cancelAndJoin(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  pauseIconDirectory.store(false);
  cancelling.join();
  assert(catalog.status() == Catalog::Status::Cached && catalog.missingIcons() == 1);
  {
    Catalog reopened;
    assert(reopened.configure(SOURCE) && reopened.missingIcons() == 1);
    assert(reopened.iconPath(entries[0], path, sizeof(path)) && soleOldIcon == path);
  }
  failIconReads.store(true);
  assert(catalog.startRefresh());
  finish(catalog);
  assert(catalog.error() == Catalog::Error::StorageFailure && Storage.exists(soleOldIcon.c_str()));
  failIconReads.store(false);
  // A failed preservation write must not delete the sole valid old copy.
  failFileWrites.store(true);
  assert(catalog.startRefresh());
  finish(catalog);
  assert(catalog.error() == Catalog::Error::StorageFailure && Storage.exists(soleOldIcon.c_str()));
  assert(catalog.iconPath(entries[0], path, sizeof(path)) && soleOldIcon == path);
  failFileWrites.store(false);
  fakeHttp::responses.erase(ICON);  // Recovery has to reuse the old image, not download it.
  const unsigned beforeRecovery = fakeHttp::requests;
  ready(catalog);
  assert(catalog.missingIcons() == 0 && fakeHttp::requests == beforeRecovery + 2);
  assert(catalog.iconPath(entries[0], path, sizeof(path)));
  fixture(icon);

  // One transient image failure is retried once; exhaustion remains visible
  // and persisted while a complete catalog stays ready/offline-readable.
  std::string freshIcon = icon;
  freshIcon[8] = 'N';
  fixture(freshIcon);
  fakeHttp::responses[ICON].transientFailures = 1;
  const unsigned beforeRetry = fakeHttp::requests;
  ready(catalog);
  assert(catalog.missingIcons() == 0 && fakeHttp::requests == beforeRetry + 4);
  freshIcon[8] = 'P';
  fixture(freshIcon);
  fakeHttp::responses[ICON].transientFailures = 2;
  const unsigned beforeExhausted = fakeHttp::requests;
  ready(catalog);
  assert(catalog.missingIcons() == 1 && fakeHttp::requests == beforeExhausted + 4);
  {
    Catalog reopened;
    assert(reopened.configure(SOURCE) && reopened.missingIcons() == 1 && reopened.total() == 21);
  }
  ready(catalog);
  assert(catalog.missingIcons() == 0);
  fixture(icon);

  const unsigned requests = fakeHttp::requests;
  {
    Catalog reopened;
    assert(reopened.configure(SOURCE) && reopened.hasCache() && reopened.total() == 21);
    assert(fakeHttp::requests == requests);  // Cached JSON is shown without network.
    assert(reopened.load(10, 16, entries, count, total) && count == 11);
  }
  WiFi.state.store(0);
  assert(!catalog.startRefresh() && catalog.error() == Catalog::Error::Offline && catalog.hasCache());
  WiFi.state.store(WL_CONNECTED);

  if (fakeMemory::psramEnabled) {
    fakeMemory::failPsram.store(true);
    {
      Catalog coldStart;
      const unsigned before = fakeHttp::requests;
      assert(!coldStart.configure(SOURCE) && coldStart.error() == Catalog::Error::NoMemory);
      assert(!coldStart.startRefresh() && fakeHttp::requests == before);
      fakeMemory::failPsram.store(false);
      assert(coldStart.configure(SOURCE) && coldStart.total() == 21);
      assert(coldStart.load(0, 4, entries, count, total) && total == 21);
      fakeMemory::failPsram.store(true);
    }
    assert(catalog.startRefresh());
    finish(catalog);
    assert(catalog.error() == Catalog::Error::NoMemory && catalog.hasCache());
    fakeMemory::failPsram.store(false);
    assert(catalog.load(0, 4, entries, count, total) && total == 21);
    assert(fakeMemory::liveBytes() == 0);
  }

  // Mid-refresh connectivity failure cannot replace the preceding complete generation.
  const unsigned revision = catalog.revision();
  fakeHttp::responses.erase(SECOND);
  assert(catalog.startRefresh());
  finish(catalog);
  assert(catalog.error() == Catalog::Error::Network && catalog.revision() == revision);
  assert(catalog.load(0, 16, entries, count, total) && total == 21);
  fixture(icon);
  failIndexRename.store(1);
  assert(catalog.startRefresh());
  finish(catalog);
  assert(catalog.error() == Catalog::Error::StorageFailure && catalog.revision() == revision);
  assert(catalog.configure(SOURCE) && catalog.total() == 21);  // Rename rollback + restart.

  // Existing cache remains queryable while the worker is blocked in network I/O.
  fixture(icon);
  fakeHttp::responses[SOURCE].block = true;
  const unsigned blocked = fakeHttp::blocked;
  assert(catalog.startRefresh());
  while (fakeHttp::blocked == blocked) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const auto started = std::chrono::steady_clock::now();
  assert(catalog.load(0, 4, entries, count, total) && count == 4);
  catalog.cancelAndJoin();
  assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
  assert(catalog.status() == Catalog::Status::Cached);

  // Source identity prevents showing a different server's old catalog.
  assert(catalog.configure("https://other.test/index.json") && !catalog.hasCache());
  assert(!catalog.lookup("app_0", entries[0]));
  assert(catalog.configure(SOURCE) && catalog.hasCache());
  assert(!catalog.configure("https://user:secret@example.test/index.json"));
  assert(catalog.configure(SOURCE));

  fixture(icon);
  fakeHttp::responses[SECOND].body = "{bad-json";
  assert(catalog.startRefresh());
  finish(catalog);
  assert(catalog.error() == Catalog::Error::InvalidCatalog && catalog.total() == 21);
  fixture(icon);
  JsonDocument altered;
  deserializeJson(altered, fakeHttp::responses[SECOND].body);
  altered["total"] = 23;
  fakeHttp::responses[SECOND].body = serialize(altered);
  assert(catalog.startRefresh());
  finish(catalog);
  assert(catalog.error() == Catalog::Error::InvalidCatalog && catalog.total() == 21);

  // Legacy catalogs without counters remain usable, but a next-link cycle is rejected.
  JsonDocument legacy;
  legacy["schema"] = 1;
  legacy["api"] = 1;
  legacy["next"] = "index.json";
  package(legacy["packages"].to<JsonArray>().add<JsonObject>(), 0);
  fakeHttp::responses[SOURCE] = {serialize(legacy)};
  const unsigned beforeLoop = fakeHttp::requests;
  assert(catalog.startRefresh());
  finish(catalog);
  assert(catalog.error() == Catalog::Error::InvalidCatalog && fakeHttp::requests == beforeLoop + 1);
  legacy["next"] = "";
  fakeHttp::responses[SOURCE] = {serialize(legacy)};
  ready(catalog);
  assert(catalog.total() == 1);

  // A digest-valid file with a wrong image header is not published as an icon.
  icon[0] = 'X';
  fixture(icon);
  ready(catalog);
  assert(catalog.lookup("app_0", entries[0]) && !catalog.iconPath(entries[0], path, sizeof(path)));
  assert(catalog.total() == 21 && catalog.missingIcons() == 1);

  // Session writes are bounded and errors never leave a completed staging file.
  HttpDownloadSession session;
  std::atomic<bool> cancel{false};
  const unsigned beforeRejected = fakeHttp::requests;
  tlsAvailable = false;
  assert(session.fetch(SOURCE, "/bounded.part", 100, cancel) == HttpDownloadSession::Result::NoMemory);
  assert(fakeHttp::requests == beforeRejected && fakeHttp::liveSockets == 0);
  tlsAvailable = true;
  // Preserve the established default-capability TLS preflight: low internal
  // capacity alone must not reject a transfer whose allocator can use PSRAM.
  fakeMemory::internalFree.store(22212);
  fakeMemory::internalLargest.store(7156);
  fakeHttp::responses["https://example.test/file"] = {"ok"};
  assert(session.fetch("https://example.test/file", "/bounded.part", 100, cancel) == HttpDownloadSession::Result::Ok);
  assert(fakeHttp::requests == beforeRejected + 1 && fakeHttp::liveSockets == 0);
  fakeMemory::internalFree.store(100000);
  fakeMemory::internalLargest.store(80000);
  fakeHttp::responses["http://test/file"] = {std::string(300, 'x')};
  assert(session.fetch("http://test/file", "/bounded.part", 100, cancel) == HttpDownloadSession::Result::TooLarge);
  assert(!Storage.exists("/bounded.part"));
  fakeHttp::responses["http://test/file"].complete = false;
  assert(session.fetch("http://test/file", "/bounded.part", 1000, cancel) == HttpDownloadSession::Result::Network);
  assert(!Storage.exists("/bounded.part"));
  failFileWrites.store(true);
  assert(session.fetch("http://test/file", "/bounded.part", 1000, cancel) ==
         HttpDownloadSession::Result::StorageFailure);
  failFileWrites.store(false);
  assert(!Storage.exists("/bounded.part"));
  assert(fakeHttp::liveSockets == 0 && fakeMemory::liveBytes() == 0);
  if (!fakeMemory::psramEnabled) assert(fakeMemory::psramCalls == 0);
  std::cout << "Store cache: range/lookup, offline restart, failed refresh/rename, source isolation, cancellation, "
               "icon preservation/read-write faults/retry/progress, bounded sessions and "
            << argv[2] << " allocation lifetime/OOM passed\n";
}
