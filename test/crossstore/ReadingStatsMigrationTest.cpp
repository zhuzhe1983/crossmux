#include <CrossStoreRuntime.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "activities/apps/appstore/LuaReadingQueries.h"

struct Day {
  uint32_t dayOrdinal;
  uint64_t readingMs;
};
struct Book {
  std::vector<Day> readingDays;
};

struct AnalyticsHost final : crossstore::Host {
  std::vector<Book> books{70};
  std::vector<Day> days{{19782, 60000}, {20727, 1200000}, {20737, 2400000}, {20738, 600000}};
  crossstore::Canvas scene;
  bool permit = true, clock = true, chineseText = false, enabled = true;
  int width = 432, height = 640, opened = 0;
  unsigned calls = 0;
  bool longTitles = false;
  AnalyticsHost() {
    books[0].readingDays = {{20727, 1200000}, {20737, 1800000}};
    books[33].readingDays = {{19782, 60000}, {20737, 600000}};
    books[69].readingDays = {{20738, 600000}};
  }
  bool allowed(const char*) const override { return permit; }
  bool system(const char* command, const char* argument, int64_t index, crossstore::ServiceReply& r) override {
    ++calls;
    if (!permit) return false;
    if (!std::strcmp(command, "reading.counts")) {
      r.a = books.size();
      r.b = 3;
      r.c = 2;
    } else if (!std::strcmp(command, "reading.summary")) {
      r.a = 4260000;
      r.b = 600000;
      r.c = 3000000;
    } else if (!std::strcmp(command, "reading.goal")) {
      r.a = 1800000;
      r.b = 4;
      r.c = days.size();
    } else if (!std::strcmp(command, "reading.calendar")) {
      if (!*argument && !clock && days.empty()) return false;
      const char* month = *argument ? argument : "2026-10";
      if (!std::strcmp(month, "2026-10")) {
        r.a = 20727;
        r.b = 31;
      } else if (!std::strcmp(month, "2026-09")) {
        r.a = 20697;
        r.b = 30;
      } else if (!std::strcmp(month, "2026-11")) {
        r.a = 20758;
        r.b = 30;
      } else if (!std::strcmp(month, "2024-02")) {
        r.a = 19754;
        r.b = 29;
      } else
        return false;
      r.c = 20738;
      std::snprintf(r.text, sizeof(r.text), "%s", month);
    } else if (!std::strcmp(command, "reading.date")) {
      if (index < 1 || index > 50037) return false;
      r.a = index;
      r.b = crossstore::reading::millisecondsOnDay(days, index);
      if (index >= 20727 && index < 20758)
        std::snprintf(r.text, sizeof(r.text), "2026-10-%02lld", static_cast<long long>(index - 20726));
      else
        std::snprintf(r.text, sizeof(r.text), "day-%lld", static_cast<long long>(index));
    } else if (!std::strcmp(command, "reading.date_book")) {
      uint32_t ordinal = 0;
      bool reverse = false;
      if (!crossstore::reading::parseDay(argument, 50037, ordinal, reverse) || index < 1 ||
          static_cast<size_t>(index) > books.size())
        return false;
      const auto value = crossstore::reading::nextBookOnDay(books, ordinal, index, reverse);
      r.a = value.book;
      r.b = value.milliseconds;
      r.c = value.next;
    } else if (!std::strcmp(command, "reading.book") || !std::strcmp(command, "reading.open") ||
               !std::strcmp(command, "reading.book_detail")) {
      if (index < 1 || static_cast<size_t>(index) > books.size()) return false;
      if (!std::strcmp(command, "reading.open"))
        opened = index;
      else if (!std::strcmp(command, "reading.book_detail")) {
        std::strcpy(r.text, "Chapter 4");
        std::strcpy(r.extra, "2026-10-01|2026-10-12|");
        r.a = 5;
        r.b = 600000;
        r.c = 70;
      } else {
        std::snprintf(r.text, sizeof(r.text), "Book %lld", static_cast<long long>(index));
        std::strcpy(r.extra, "A real fixture author");
        r.a = 3600000;
        r.b = 25;
        if (longTitles) {
          std::memset(r.text, 'T', 192);
          r.text[192] = 0;
          std::memset(r.extra, 'A', 76);
          r.extra[76] = 0;
        }
      }
    } else if (!std::strcmp(command, "reading.achievements")) {
      r.a = enabled ? 62 : 0;
      r.b = enabled ? 3 : 0;
      r.c = enabled;
    } else if (!std::strcmp(command, "reading.achievement")) {
      if (!enabled || index < 1 || index > 62) return false;
      r.a = index % 8;
      r.b = index * 5;
      r.c = index <= 3;
      std::snprintf(r.text, sizeof(r.text), "Achievement %lld", static_cast<long long>(index));
      if (r.c) std::strcpy(r.extra, "2026-10-12");
      if (longTitles) {
        std::memset(r.text, 'U', 192);
        r.text[192] = 0;
      }
    } else
      return false;
    return true;
  }
  void title(const char*) override {}
  void clear() override { scene.clear(); }
  void grid(unsigned) override {}
  void caption(const char*) override {}
  bool row(const char*, const char*) override { return true; }
  bool capability(const char*) const override { return false; }
  bool chinese() const override { return chineseText; }
  uint32_t milliseconds() const override { return 1; }
  bool read(const char*, char*, size_t) override {
    assert(false);
    return false;
  }
  bool write(const char*, const char*, size_t) override {
    assert(false);
    return false;
  }
  void input(const char*, const char*) override {}
  void notify(const char*) override {}
  bool open(const char*) override { return true; }
  void size(int& w, int& h) const override {
    w = width;
    h = height;
  }
  bool canvas() override { return true; }
  bool draw(const crossstore::DrawCommand& c, const char* label) override {
    if (c.x < 0 || c.y < 0 || c.x + c.width > width + 1 || c.y + c.height > height + 1) {
      std::fprintf(stderr, "Outside viewport: %d %d,%d %dx%d in %dx%d\n", int(c.kind), c.x, c.y, c.width, c.height,
                   width, height);
      return false;
    }
    if (c.kind == crossstore::DrawKind::Button || c.kind == crossstore::DrawKind::Hit) {
      for (size_t i = 0; i < scene.controlCount; ++i) {
        const auto& b = scene.commands[scene.controls[i]];
        const int dx = std::max(c.x - (b.x + b.width), b.x - (c.x + c.width));
        const int dy = std::max(c.y - (b.y + b.height), b.y - (c.y + c.height));
        assert(dx >= 6 || dy >= 6);
      }
    }
    return scene.add(c, label);
  }
  bool contains(const char* value) const {
    for (size_t i = 0; i < scene.count; ++i)
      if (std::strstr(scene.text + scene.commands[i].text, value)) return true;
    return false;
  }
  void measure(const char*, int, unsigned, int& w, int& h) override {
    w = 20;
    h = 20;
  }
};

static void queryTests() {
  using namespace crossstore::reading;
  std::vector<Book> books(70);
  books[0].readingDays = {{10, 60000}, {12, 120000}};
  books[33].readingDays = {{12, 90000}};
  books[69].readingDays = {{11, 30000}};
  assert(millisecondsOnDay(books[0].readingDays, 11) == 0);
  assert(millisecondsOnDay(books[0].readingDays, 12) == 120000);
  auto match = nextBookOnDay(books, 12, 1, false);
  assert(match.book == 1 && match.next == 2 && match.milliseconds == 120000);
  match = nextBookOnDay(books, 12, 2, false);
  assert(match.book == 0 && match.next == 34);  // bounded to 32, even with a match just outside.
  match = nextBookOnDay(books, 12, 34, false);
  assert(match.book == 34 && match.next == 35 && match.milliseconds == 90000);
  match = nextBookOnDay(books, 12, 70, true);
  assert(match.book == 0 && match.next == 38);
  match = nextBookOnDay(books, 12, 38, true);
  assert(match.book == 34 && match.next == 33);
  assert(nextBookOnDay(books, 11, 70, false).next == 0);
  assert(nextBookOnDay(books, 12, 1, true).next == 0);
  assert(nextBookOnDay(books, 12, 0, false).book == 0);
  assert(nextBookOnDay(books, 12, 71, false).book == 0);
  uint32_t day = 0;
  bool reverse = false;
  assert(parseDay("20737", 50037, day, reverse) && day == 20737 && !reverse);
  assert(parseDay("-20737", 50037, day, reverse) && reverse);
  for (const char* bad : {"", "-", "0", "--12", "12x", "/private", "9999999999999999999", "50038"})
    assert(!parseDay(bad, 50037, day, reverse));
  int year = 0;
  unsigned month = 0;
  assert(parseMonth("2024-02", year, month) && year == 2024 && month == 2);
  assert(parseMonth("2106-12", year, month));
  for (const char* bad : {"", "2026-1", "2026-00", "2026-13", "1969-12", "2107-01", "2026-01/", "../../x"})
    assert(!parseMonth(bad, year, month));
  std::puts("Reading queries: sorted days, sparse scans, reverse cursors, invalid input passed");
}

int main(int argc, char** argv) {
  assert(argc == 2);
  queryTests();
  std::ifstream file(argv[1]);
  assert(file.good());
  const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  for (const auto size :
       {std::make_pair(336, 430), std::make_pair(432, 640), std::make_pair(912, 420), std::make_pair(492, 850)}) {
    for (bool chinese : {false, true}) {
      AnalyticsHost host;
      host.width = size.first;
      host.height = size.second;
      host.chineseText = chinese;
      crossstore::Runtime vm(host);
      auto event = [&](const char* name, int id = 0, int x = -1, int y = -1) {
        host.calls = 0;
        if (!vm.event(name, id, "", x, y)) {
          std::fprintf(stderr, "%s %d: %s\n", name, id, vm.error());
          std::abort();
        }
        assert(host.calls <= 70);  // 62 achievements, or a month plus metadata.
      };
      if (!vm.load(source.data(), source.size(), 65536)) {
        std::fprintf(stderr, "load: %s\n", vm.error());
        return 1;
      }
      event("on_select", 1);
      assert(host.contains("Book 1"));
      event("on_select", 21);
      assert(host.contains("Book 1"));
      event("on_select", 7);
      assert(host.contains("2026-10-01"));
      event("on_select", 6);
      assert(host.opened == 1);
      event("on_back");
      event("on_back");
      event("on_select", 2);
      assert(host.contains("2026-10"));
      event("on_select", 10);
      assert(host.contains("2026-09"));
      event("on_select", 11);
      assert(host.contains("2026-10"));
      event("on_select", 13);  // October 11, two matches in a sparse 70-book fixture.
      event("on_select", 14);
      assert(host.contains("Book 1"));
      assert(host.contains("Book 34"));
      event("on_select", 22);
      assert(host.contains("Book 34"));
      event("on_select", 6);
      assert(host.opened == 34);
      event("on_back");
      event("on_select", 5);  // advance if a very small viewport stops before the end.
      event("on_select", 4);
      event("on_back");
      const int tapX = static_cast<int>(3.5 * (host.width + 6) / 7 - 3);
      const int tapY = 108 + (host.height - 218) / 6 / 2;
      event("on_select", 12, tapX, tapY);
      assert(host.contains("2026-10-01"));
      event("on_select", 12);  // Confirm on the calendar opens the selected date.
      assert(host.contains("Book 1"));
      event("on_back");
      event("on_back");
      event("on_select", 3);
      assert(host.contains("Achievement 4"));
      event("on_select", 5);
      assert(!host.contains("Achievement 4"));
      event("on_select", 17);
      assert(host.contains("Achievement 1"));
      assert(host.contains("2026-10-12"));
      host.longTitles = true;
      for (int cycle = 0; cycle < 20; ++cycle) {
        event("on_back");
        event("on_select", 1);
        event("on_select", 21);
        event("on_select", 7);
        event("on_back");
        event("on_back");
        event("on_select", 3);
        event("on_select", 16);
        event("on_select", 5);
      }
      std::printf("live after long-title stress=%zu ", vm.used());
      host.enabled = false;
      event("on_resize");
      assert(host.scene.controlCount == 0);
      host.permit = false;
      event("on_resize");
      assert(host.scene.controlCount == 0);
      std::printf("Reading app %dx%d %s peak=%zu / 65536\n", size.first, size.second, chinese ? "zh" : "en", vm.peak());
      vm.close();
      assert(vm.used() == 0);
      host.permit = true;
      host.books.clear();
      host.days.clear();
      host.clock = false;
      assert(vm.load(source.data(), source.size(), 65536));
      event("on_select", 2);
      assert(host.scene.controlCount == 0);
      event("on_back");
      event("on_select", 1);
      assert(host.scene.controlCount == 0);
    }
  }
}
