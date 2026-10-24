#pragma once

#include <WeReadStore.h>

#include <cstdint>
#include <cstring>

// One SD-backed TOC, with one record supplied by the service. No chapter list
// is retained in RAM, even for books with thousands of chapters.
class LuaWeReadCache {
 public:
  enum class State : uint8_t { Missing, Ready, Invalid };

  ~LuaWeReadCache() { close(); }

  State open(const char* bookId) {
    close();
    if (!bookId || !bookId[0] || std::strlen(bookId) >= sizeof(bookId_)) return State::Invalid;
    const auto path = WeReadStore::tocPath(bookId);
    if (!Storage.exists(path.c_str())) return State::Missing;
    if (!WeReadStore::openToc(path, toc_, count_)) {
      close();
      return State::Invalid;
    }
    std::strcpy(bookId_, bookId);
    return State::Ready;
  }

  bool read(int64_t index, WeReadStore::TocRecord& record, bool& cached) {
    cached = false;
    if (index < 0 || static_cast<uint64_t>(index) >= count_ ||
        !WeReadStore::readTocRecord(toc_, static_cast<uint32_t>(index), record) ||
        !std::memchr(record.title, 0, sizeof(record.title)))
      return false;
    const auto path = WeReadStore::chapterPath(WeReadStore::bookDirectory(bookId_), static_cast<uint32_t>(index));
    cached = Storage.exists(path.c_str());
    return true;
  }

  uint32_t count() const { return count_; }

  void close() {
    toc_.close();
    count_ = 0;
    bookId_[0] = 0;
  }

 private:
  HalFile toc_;
  char bookId_[64]{};
  uint32_t count_ = 0;
};
