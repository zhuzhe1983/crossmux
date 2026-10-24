#include <CrossStoreRuntime.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

struct Host : crossstore::Host {
  crossstore::Canvas scene;
  int width = 432, height = 640;
  unsigned revision = 1, requests = 0, opens = 0;
  unsigned covers = 0;
  int selected = 1, savedPolicy = 1;
  int64_t lastChapter = -1;
  bool permitted = true, downloaded = true, corruptRow = false;
  std::string state = "idle", cacheState = "ready", arguments;
  int chapters = 25;
  void title(const char*) override {}
  void clear() override { scene.clear(); }
  void grid(unsigned) override {}
  void caption(const char*) override {}
  bool row(const char*, const char*) override { return true; }
  bool capability(const char*) const override { return true; }
  bool chinese() const override { return false; }
  uint32_t milliseconds() const override { return 1; }
  bool read(const char*, char*, size_t) override { return false; }
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
  void measure(const char* value, int maximum, unsigned, int& w, int& h) override {
    int full = std::strlen(value) * 10;
    w = std::min(maximum, full);
    h = std::max(1, (full + maximum - 1) / maximum) * 24;
  }
  bool draw(const crossstore::DrawCommand& command, const char* text) override {
    assert(command.x >= 0 && command.y >= 0);
    assert(command.x + command.width <= width && command.y + command.height <= height);
    if (command.kind == crossstore::DrawKind::Button || command.kind == crossstore::DrawKind::Hit) {
      for (size_t i = 0; i < scene.controlCount; ++i) {
        const auto& other = scene.commands[scene.controls[i]];
        assert(command.x >= other.x + other.width + 6 || other.x >= command.x + command.width + 6 ||
               command.y >= other.y + other.height + 6 || other.y >= command.y + command.height + 6);
      }
    }
    return scene.add(command, text);
  }
  bool contains(const char* value) const {
    for (size_t i = 0; i < scene.count; ++i)
      if (std::strstr(scene.text + scene.commands[i].text, value)) return true;
    return false;
  }
  bool system(const char* name, const char* argument, int64_t index, crossstore::ServiceReply& reply) override {
    if (!permitted) return false;
    if (!std::strcmp(name, "weread.start") || !std::strcmp(name, "power.keep_awake")) return true;
    if (!std::strcmp(name, "weread.status")) {
      std::strcpy(reply.text, state.c_str());
      reply.a = 2;
      reply.b = revision;
      return true;
    }
    if (!std::strcmp(name, "weread.book")) {
      if (index < 1 || index > 2) return false;
      std::strcpy(reply.text, index == 1 ? "First title" : "Second title");
      std::strcpy(reply.extra, "Author");
      reply.a = downloaded;
      return true;
    }
    if (!std::strcmp(name, "weread.cover")) {
      int x, y, w, h;
      char tail;
      assert(index >= 1 && index <= 2);
      assert(std::sscanf(argument, "%d:%d:%d:%d%c", &x, &y, &w, &h, &tail) == 4);
      assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= width && y + h <= height);
      ++covers;
      return true;
    }
    if (!std::strcmp(name, "weread.detail")) return false;
    if (!std::strcmp(name, "weread.cache")) {
      selected = index;
      std::strcpy(reply.text, cacheState.c_str());
      reply.a = downloaded;
      reply.b = cacheState == "ready" ? chapters : 0;
      reply.c = selected == 1 ? savedPolicy : 0;
      return true;
    }
    if (!std::strcmp(name, "weread.cached_chapter")) {
      assert(index >= 0 && index < chapters);
      lastChapter = index;
      if (corruptRow) return false;
      std::snprintf(reply.text, sizeof(reply.text), "Chapter %lld", static_cast<long long>(index + 1));
      reply.a = index % 2 == 0;
      reply.b = index == 1;
      reply.c = 1000;
      return true;
    }
    if (!std::strcmp(name, "weread.open")) {
      ++opens;
      return downloaded;
    }
    if (!std::strcmp(name, "weread.download")) {
      ++requests;
      arguments = argument;
      state = "busy";
      ++revision;
      return true;
    }
    if (!std::strcmp(name, "weread.progress")) {
      reply.a = 1;
      reply.b = 2;
      return true;
    }
    assert(false && "unexpected service command");
    return false;
  }
};
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1]);
  std::string source((std::istreambuf_iterator<char>(input)), {});
  assert(!source.empty());
  const int sizes[][2] = {{336, 430}, {432, 640}, {912, 420}, {492, 850}};
  for (const auto& size : sizes) {
    Host host;
    host.width = size[0];
    host.height = size[1];
    crossstore::Runtime vm(host);
    assert(vm.load(source.data(), source.size(), 131072));
    auto event = [&](const char* name, int value = 0) {
      if (!vm.event(name, value)) {
        std::fprintf(stderr, "%s\n", vm.error());
        std::abort();
      }
    };
    event("on_start");
    event("on_tick");
    assert(host.covers == 2);  // Existing visible shelf only; no explicit network request.
    event("on_select", 101);
    assert(host.contains("Saved for offline reading"));
    event("on_select", 16);
    assert(host.contains("Images: text only"));
    event("on_select", 14);
    event("on_select", 42);
    event("on_select", 16);
    assert(host.contains("Images: included"));  // Returning to details preserves the uncommitted choice.
    event("on_select", 42);
    event("on_select", 17);
    assert(host.contains("Local chapters") && host.contains("Chapter file saved") && host.contains("Not cached"));
    assert(host.contains("Paid chapter"));
    auto firstPageLast = host.lastChapter;
    event("on_select", 51);
    assert(host.lastChapter > firstPageLast);
    for (int i = 0; i < 40; ++i) event("on_select", 51);
    assert(host.lastChapter == 24);
    event("on_select", 50);
    assert(host.lastChapter < 24);
    host.corruptRow = true;
    event("on_resize");
    assert(host.contains("Read error"));
    host.corruptRow = false;
    assert(host.requests == 0);  // All inspection, paging and option changes are offline and read-only.
    event("on_back");
    assert(host.contains("Download options"));
    event("on_select", 11);
    assert(host.arguments == "images" && host.requests == 1);
    host.savedPolicy = 0;
    host.state = "done";
    ++host.revision;
    event("on_service");
    assert(host.contains("Saved for offline reading"));
    event("on_select", 16);
    assert(host.contains("Images: included"));
    event("on_select", 42);
    event("on_select", 7);
    event("on_select", 102);
    event("on_select", 16);
    assert(host.contains("Images: included"));
    event("on_select", 42);
    host.downloaded = false;
    host.cacheState = "missing";
    event("on_select", 42);
    event("on_select", 17);
    assert(host.contains("No chapter index"));
    event("on_select", 10);
    assert(host.contains("Download this book first"));
    host.cacheState = "invalid";
    event("on_select", 42);
    event("on_select", 17);
    assert(host.contains("chapter index cannot be read"));
    std::printf("WeRead Lua offline flows %dx%d peak %zu bytes\n", host.width, host.height, vm.peak());
  }
  Host denied;
  denied.permitted = false;
  crossstore::Runtime vm(denied);
  assert(vm.load(source.data(), source.size(), 131072));
  assert(vm.event("on_start") && vm.event("on_tick"));
  assert(denied.contains("Allow reading data"));
}
