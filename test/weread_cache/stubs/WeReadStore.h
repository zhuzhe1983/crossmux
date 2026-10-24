#pragma once
#include <cstdint>
#include <set>
#include <string>
struct HalFile {
  bool opened = false;
  void close() { opened = false; }
};
struct TestStorage {
  std::set<std::string> paths;
  bool exists(const char* path) { return paths.count(path); }
};
extern TestStorage Storage;
namespace WeReadStore {
struct TocRecord {
  char title[192]{};
};
std::string tocPath(const char*);
std::string bookDirectory(const char*);
std::string chapterPath(const std::string&, uint32_t);
bool openToc(const std::string&, HalFile&, uint32_t&);
bool readTocRecord(HalFile&, uint32_t, TocRecord&);
}  // namespace WeReadStore
