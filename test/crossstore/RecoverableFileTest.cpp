#include <cassert>
#include <cstdio>
#include <map>
#include <string>

#include "crossstore/RecoverableFile.h"

struct Files {
  std::map<std::string, std::string> data;
  int failRename = -1, renames = 0;
  bool exists(const char* path) { return data.count(path); }
  bool remove(const char* path) { return data.erase(path); }
  bool rename(const char* from, const char* to) {
    if (renames++ == failRename || !exists(from) || exists(to)) return false;
    data[to] = data.at(from);
    data.erase(from);
    return true;
  }
};

int main() {
  for (int failure = -1; failure < 3; ++failure) {
    Files files{{{"app", "old"}, {"download.part", "new"}}, failure};
    const bool done = crossstore::commitFile(files, "download.part", "app");
    files.failRename = -1;
    assert(crossstore::recoverFile(files, "app"));
    assert(files.data.at("app") == (done ? "new" : "old"));
  }
  // Power loss after preserving old, then after installing new.
  Files before{{{"app.previous", "old"}, {"download.part", "new"}}};
  assert(crossstore::recoverFile(before, "app"));
  assert(before.data.at("app") == "old");
  Files after{{{"app.previous", "old"}, {"app", "new"}}};
  assert(crossstore::recoverFile(after, "app"));
  assert(after.data.at("app") == "new");
  assert(!after.exists("app.previous"));
  std::puts("CrossStore install: rename failure and interrupted-update recovery passed");
}
