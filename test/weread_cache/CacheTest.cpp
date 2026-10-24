#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>

#include "activities/apps/appstore/LuaWeReadCache.h"

TestStorage Storage;
bool validHeader = true, readable = true, validTitle = true;
unsigned reads = 0;
namespace WeReadStore {
std::string bookDirectory(const char* id) { return std::string("/cache/") + id; }
std::string tocPath(const char* id) { return bookDirectory(id) + "/toc.bin"; }
std::string chapterPath(const std::string& dir, uint32_t index) { return dir + "/" + std::to_string(index); }
bool openToc(const std::string&, HalFile& file, uint32_t& count) {
  file.opened = true;
  count = 5000;
  return validHeader;
}
bool readTocRecord(HalFile& file, uint32_t index, TocRecord& record) {
  ++reads;
  if (!file.opened || !readable) return false;
  if (validTitle)
    std::snprintf(record.title, sizeof(record.title), "Chapter %u", index);
  else
    std::memset(record.title, 'X', sizeof(record.title));
  return true;
}
}  // namespace WeReadStore
int main() {
  LuaWeReadCache cache;
  WeReadStore::TocRecord record;
  bool saved = true;
  assert(cache.open("first") == LuaWeReadCache::State::Missing);
  assert(cache.count() == 0 && !cache.read(0, record, saved) && !saved);
  Storage.paths = {"/cache/first/toc.bin", "/cache/first/0"};
  assert(cache.open("first") == LuaWeReadCache::State::Ready && cache.count() == 5000);
  assert(cache.read(0, record, saved) && saved);
  assert(cache.read(1, record, saved) && !saved);
  assert(cache.read(4999, record, saved) && !saved);
  auto before = reads;
  assert(!cache.read(-1, record, saved));
  assert(!cache.read(5000, record, saved));
  assert(!cache.read(INT64_MAX, record, saved));
  assert(reads == before);
  readable = false;
  assert(!cache.read(0, record, saved) && !saved);
  readable = true;
  validTitle = false;
  assert(!cache.read(0, record, saved));
  validTitle = true;
  validHeader = false;
  assert(cache.open("first") == LuaWeReadCache::State::Invalid && cache.count() == 0);
  assert(!cache.read(0, record, saved));
  validHeader = true;
  assert(cache.open("first") == LuaWeReadCache::State::Ready);
  assert(cache.open("second") == LuaWeReadCache::State::Missing);
  assert(!cache.read(0, record, saved));
  assert(cache.open(nullptr) == LuaWeReadCache::State::Invalid);
  assert(cache.open("") == LuaWeReadCache::State::Invalid);
  assert(cache.open(std::string(64, 'x').c_str()) == LuaWeReadCache::State::Invalid);
  cache.close();
  cache.close();
  assert(cache.count() == 0);
  std::puts("WeRead cache: missing/invalid/partial cache, boundaries, stale selection and cleanup passed");
}
