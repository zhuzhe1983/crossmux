#include <CrossStoreRuntime.h>
#include <CrossStoreSvg.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "activities/apps/buddy/BuddyGenerator.h"
#include "activities/apps/standby/ChineseAlmanac.h"

struct Host final : crossstore::Host {
  int rows = 0, notices = 0;
  mutable uint32_t now = 1;
  uint32_t clockReadStep = 0, storageDelayMs = 0;
  unsigned storageWrites = 0;
  bool failWrites = false;
  bool stressChat = false;
  std::string chatPeer;
  std::string heading, packagePath;
  std::string identity = "288485447014";
  std::string today = "2026-10-23";
  bool useChinese = false, clockValid = true;
  bool airImage = false;
  std::string wereadState = "idle";
  unsigned serviceRevision = 1;
  bool allowed(const char*) const override { return true; }
  bool system(const char* command, const char*, int64_t index, crossstore::ServiceReply& reply) override {
    if (!std::strncmp(command, "airpage.", 8)) {
      if (!std::strcmp(command, "airpage.status")) {
        std::strcpy(reply.text, "ready");
        reply.a = 20;
        reply.b = serviceRevision;
        reply.c = airImage;
      } else if (!std::strcmp(command, "airpage.url"))
        std::strcpy(reply.text, "https://airpage.uipcat.com/?id=0123456789abcdef&w=480&h=800&mode=gray4");
      else if (!std::strcmp(command, "airpage.history")) {
        std::strcpy(reply.text, "2026102312020300");
        reply.a = 480;
        reply.b = 800;
      } else if (!std::strcmp(command, "airpage.show")) {
        airImage = true;
        ++serviceRevision;
      } else if (!std::strcmp(command, "airpage.hide")) {
        airImage = false;
        ++serviceRevision;
      }
      return true;
    }
    if (!std::strncmp(command, "weread.", 7)) {
      if (!std::strcmp(command, "weread.status")) {
        std::snprintf(reply.text, sizeof(reply.text), "%s", wereadState.c_str());
        reply.a = 23;
        reply.b = serviceRevision;
      } else if (!std::strcmp(command, "weread.book")) {
        if (index < 1 || index > 23) return false;
        std::strcpy(reply.text, "A Reader's Journey");
        std::strcpy(reply.extra, "An Author");
        reply.a = 1;
      } else if (!std::strcmp(command, "weread.detail")) {
        std::strcpy(reply.text, "A Reader's Journey");
        std::strcpy(reply.extra, "Publisher");
        reply.a = 12000;
        reply.b = 80;
        reply.c = 1000;
      } else if (!std::strcmp(command, "weread.intro")) {
        std::strcpy(reply.text,
                    "A thoughtful introduction to a reader's journey, with enough words to check pagination across "
                    "different screen sizes and to preserve every complete character when moving to the next page.");
        reply.a = std::strlen(reply.text);
        reply.b = 1000;
      } else if (!std::strcmp(command, "weread.progress")) {
        reply.a = 12;
        reply.b = 40;
      } else if (!std::strcmp(command, "weread.qr")) {
        if (index) return false;
        std::strcpy(reply.text, "https://weread.qq.com/web/confirm?uid=test");
      } else if (!std::strcmp(command, "weread.chapter_count"))
        reply.a = 89;
      else if (!std::strcmp(command, "weread.chapter")) {
        if (index >= 89) return false;
        std::strcpy(reply.text, "A chapter heading");
      } else if (!std::strcmp(command, "weread.sync") || !std::strcmp(command, "weread.download") ||
                 !std::strcmp(command, "weread.fetch_detail") || !std::strcmp(command, "weread.range")) {
        wereadState = "busy";
        ++serviceRevision;
      } else if (!std::strcmp(command, "weread.accept") || !std::strcmp(command, "weread.cancel")) {
        wereadState = "idle";
        ++serviceRevision;
      }
      return true;
    }
    if (!std::strcmp(command, "device.id"))
      std::snprintf(reply.text, sizeof(reply.text), "%s", identity.c_str());
    else if (!std::strcmp(command, "device.time")) {
      if (!clockValid) return false;
      std::strcpy(reply.text, "12:34");
      std::snprintf(reply.extra, sizeof(reply.extra), "%s", today.c_str());
      reply.a = 5;
    } else if (!std::strcmp(command, "device.battery"))
      reply.a = 82;
    else if (!std::strcmp(command, "power.keep_awake")) {
    } else if (!std::strcmp(command, "pixel.start")) {
    } else if (!std::strcmp(command, "pixel.status")) {
      std::strcpy(reply.text, "online");
      reply.a = 1;
    } else if (!std::strcmp(command, "pixel.row")) {
      for (int i = 0; i < 60; ++i) reply.text[i] = "FA50"[(i + index) % 4];
      reply.text[60] = 0;
    } else if (!std::strcmp(command, "reading.counts")) {
      reply.a = 20;
      reply.b = 3;
      reply.c = 8;
    } else if (!std::strcmp(command, "reading.summary")) {
      reply.a = 36000000;
      reply.b = 1200000;
      reply.c = 7200000;
    } else if (!std::strcmp(command, "reading.goal")) {
      reply.a = 1800000;
      reply.b = 12;
      reply.c = 32;
    } else if (!std::strcmp(command, "reading.book")) {
      if (index < 1 || index > 20) return false;
      std::strcpy(reply.text, "A book about reading");
      std::strcpy(reply.extra, "Author");
      reply.a = 7200000;
      reply.b = 42;
    } else
      return false;
    return true;
  }
  bool contains(const char* text) const {
    for (size_t i = 0; i < scene.count; ++i)
      if (!std::strcmp(scene.text + scene.commands[i].text, text)) return true;
    return false;
  }
  void measure(const char* text, int maximum, unsigned, int& width, int& height) override {
    const int full = static_cast<int>(std::strlen(text)) * 12;
    width = std::min(maximum, full);
    height = std::max(1, (full + maximum - 1) / maximum) * 30;
  }
  bool qr(int x, int y, int size, const char*) override {
    crossstore::DrawCommand c;
    c.kind = crossstore::DrawKind::Qr;
    c.x = x;
    c.y = y;
    c.width = c.height = size;
    return scene.add(c, "");
  }
  bool chat(const char* command, const char* argument, int64_t index, const char*,
            crossstore::ServiceReply& reply) override {
    if (!std::strcmp(command, "start")) chatPeer = !std::strcmp(argument, "wifi") ? "AABBCCDD" : "*";
    if (!std::strcmp(command, "snapshot")) {
      std::strcpy(reply.text, "online");
      std::snprintf(reply.extra, sizeof(reply.extra), "%076d", 1);
      reply.a = 1;
      reply.b = stressChat ? 16 : 1;
      reply.c = stressChat ? 24 : 3;
    } else if (!std::strcmp(command, "contact") || !std::strcmp(command, "nearby")) {
      std::strcpy(reply.text, "AABBCCDD");
      reply.a = 1;
    } else if (!std::strcmp(command, "message")) {
      const char* texts[] = {"Hello!", "Nice to see you here.", "A failed message can be retried."};
      if (index < 1 || index > (stressChat ? 24 : 3)) return false;
      if (stressChat) {
        std::memset(reply.text, 'W', 192);
        reply.text[192] = 0;
      } else
        std::strcpy(reply.text, texts[index - 1]);
      std::snprintf(reply.extra, sizeof(reply.extra), "%s", chatPeer.c_str());
      reply.a = index % 5;
      reply.b = 0x80000000ULL + index;
    } else if (!std::strcmp(command, "nearby_count"))
      reply.a = 1;
    return true;
  }
  bool asset(const char* name, size_t offset, char* out, size_t capacity, size_t& bytes) override {
    std::ifstream file(packagePath + "/" + name, std::ios::binary);
    if (!file.good()) return false;
    file.seekg(offset);
    file.read(out, capacity);
    bytes = file.gcount();
    return true;
  }
  std::string captionText, route;
  std::vector<std::string> labels;
  std::map<std::string, std::string> storage;
  crossstore::Canvas scene;
  int viewportWidth = 432, viewportHeight = 640;
  void title(const char* text) override { heading = text; }
  void clear() override {
    rows = 0;
    labels.clear();
    scene.clear();
  }
  void grid(unsigned) override {}
  void caption(const char* text) override { captionText = text; }
  bool row(const char* text, const char*) override {
    labels.emplace_back(text);
    return rows++ < 32;
  }
  bool capability(const char* key) const override { return !std::strcmp(key, "wifi"); }
  bool chinese() const override { return useChinese; }
  uint32_t milliseconds() const override {
    const auto result = now;
    now += clockReadStep;
    return result;
  }
  bool read(const char* key, char* out, size_t capacity) override {
    now += storageDelayMs;
    const auto entry = storage.find(key);
    if (entry == storage.end() || entry->second.size() >= capacity) return false;
    std::memcpy(out, entry->second.c_str(), entry->second.size() + 1);
    return true;
  }
  bool write(const char* key, const char* value, size_t size) override {
    now += storageDelayMs;
    ++storageWrites;
    if (failWrites) return false;
    storage[key].assign(value, size);
    return true;
  }
  void input(const char*, const char*) override {}
  void notify(const char*) override { ++notices; }
  bool open(const char* value) override {
    route = value;
    return true;
  }
  void size(int& width, int& height) const override {
    width = viewportWidth;
    height = viewportHeight;
  }
  bool canvas() override { return true; }
  bool draw(const crossstore::DrawCommand& command, const char* label) override {
    const bool interactive = command.kind == crossstore::DrawKind::Button || command.kind == crossstore::DrawKind::Hit;
    if (interactive && (command.x < 0 || command.y < 0 || command.x + command.width > viewportWidth ||
                        command.y + command.height > viewportHeight)) {
      std::fprintf(stderr, "Out of bounds: action %d %d,%d %dx%d (viewport %dx%d)\n", command.action, command.x,
                   command.y, command.width, command.height, viewportWidth, viewportHeight);
      return false;
    }
    return scene.add(command, label);
  }
  void preview(const char* id) const {
    const char* directory = std::getenv("CROSSSTORE_PREVIEW");
    if (!directory) return;
    std::ofstream out(std::string(directory) + "/" + id + ".json");
    out << "{\"width\":" << viewportWidth << ",\"height\":" << viewportHeight << ",\"commands\":[";
    for (size_t i = 0; i < scene.count; ++i) {
      const auto& c = scene.commands[i];
      if (i) out << ',';
      out << '[' << static_cast<int>(c.kind) << ',' << c.x << ',' << c.y << ',' << c.width << ',' << c.height << ','
          << static_cast<int>(c.gray) << ',' << static_cast<int>(c.radius) << ',' << static_cast<int>(c.font) << ','
          << static_cast<int>(c.align) << ',' << static_cast<int>(c.style) << ',' << std::quoted(scene.text + c.text)
          << ']';
    }
    out << "]}";
  }
  std::string display(unsigned alignment) const {
    for (size_t i = 0; i < scene.count; ++i) {
      const auto& command = scene.commands[i];
      if (command.kind == crossstore::DrawKind::Text && command.font == 3 && command.align == alignment)
        return scene.text + command.text;
    }
    return {};
  }
};

int main(int argc, char** argv) {
  Host host;
  crossstore::Runtime vm(host);
  auto load = [&](const char* source) { return vm.load(source, std::strlen(source)); };
  // Only API 6's radio URL gets the larger bound. Other service arguments and
  // chat peer IDs retain their original limits, including similarly named calls.
  assert(load("app.system('radio.play','https://example.test/'..string.rep('x',490)); return {}"));
  assert(!load("app.system('radio.play','https://example.test/'..string.rep('x',491)); return {}"));
  assert(!load("app.system('radio.status',string.rep('x',96)); return {}"));
  assert(!load("app.chat('radio.play',string.rep('x',96)); return {}"));
  assert(
      load("assert(os==nil and io==nil and debug==nil and package==nil and load==nil and pcall==nil);"
           "return {on_start=function() app.title(app.tr('Calculator','计算器')); app.row('1'); end,"
           "on_select=function(i) assert(i==7); app.notify('message'); end}"));
  assert(host.rows == 1 && host.heading == "Calculator");
  assert(vm.event("on_select", 7) && host.notices == 1);
  assert(vm.peak() <= crossstore::MAX_VM_BYTES);
  vm.close();
  assert(vm.used() == 0);
  // SD latency must not consume Lua's CPU window. Exercise both the finite
  // callback and runaway-I/O boundaries, including the millis() rollover.
  host.storageDelayMs = 250;
  const char* slowIo =
      "return {on_start=function() app.read('slow'); app.write('slow','1'); "
      "local n=0 for i=1,3000 do n=n+i end assert(n==4501500) end}";
  assert(load(slowIo));
  assert(vm.event("on_start"));
  host.now = UINT32_MAX - 80;
  assert(load(slowIo));
  assert(!load("return {on_start=function() for i=1,20 do app.write('slow','1') end end}"));
  assert(std::strstr(vm.error(), "I/O budget"));
  host.storageDelayMs = 0;
  host.now = 1;
  host.clockReadStep = 60;
  assert(!load("return {on_start=function() local n=0 for i=1,10000 do n=n+i end end}"));
  assert(std::strstr(vm.error(), "execution budget"));
  host.clockReadStep = 0;
  host.now = 1;
  assert(!load("return {on_start=function() while true do end end}"));
  assert(std::strstr(vm.error(), "budget"));
  assert(vm.used() == 0);
  assert(!load("return {on_start=function() local t={} for i=1,99999 do t[i]=i end end}"));
  assert(vm.used() == 0);
  assert(load("return setmetatable({}, {__index=function() while true do end end})"));
  assert(vm.event("missing"));
  vm.close();
  assert(!load("return {on_start=42}"));
  assert(vm.used() == 0);
  assert(!load("return {on_start=function() error({}) end}"));
  assert(!load("\x1bLua"));
  assert(load("return {on_input=function(i,t) app.row(t) end}"));
  assert(vm.event("on_input", 0, "safe"));
  assert(!vm.event("on_input", 0, std::string(90000, 'x').c_str()));
  assert(vm.used() == 0);
  assert(load("return {on_start=function() app.clear();app.canvas();app.button(1,0,0,50,50,'A');end}"));
  assert(host.scene.controlCount == 1);
  vm.close();
  assert(
      !load("return {on_start=function() "
            "app.clear();app.canvas();app.button(1,0,0,50,50,'A');app.button(2,20,0,50,50,'B');end}"));
  assert(!load("return {on_start=function() app.box(0/0,0,10,10) end}"));
  assert(!load("return {on_start=function() app.clear();for i=1,513 do app.box(0,0,1,1) end end}"));
  assert(!load("return {on_start=function() app.clear();app.raster(0,0,10,10,2,2,'FFF') end}"));
  assert(!load("return {on_start=function() app.clear();app.raster(0,0,10,10,2,2,'FFG0') end}"));
  assert(load("return {on_start=function() app.clear();app.raster(0,0,10,10,2,2,'FA50') end}"));
  assert(host.scene.count == 1 && host.scene.textUsed == 5);
  vm.close();
  std::puts("CrossStore VM: callbacks, quotas, sandbox, invalid entry, OOM recovery passed");
  if (argc == 2) {
    auto package = [&](const char* id) {
      host.packagePath = std::string(argv[1]) + "/" + id;
      std::ifstream file(host.packagePath + "/main.lua");
      assert(file.good());
      const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      std::ifstream manifestFile(host.packagePath + "/manifest.json");
      const std::string manifest((std::istreambuf_iterator<char>(manifestFile)), std::istreambuf_iterator<char>());
      const auto key = manifest.find("\"memory_bytes\"");
      assert(key != std::string::npos);
      const auto budget = std::strtoul(manifest.c_str() + manifest.find(':', key) + 1, nullptr, 10);
      if (!vm.load(source.data(), source.size(), budget)) {
        std::fprintf(stderr, "%s: %s\n", id, vm.error());
        std::abort();
      }
    };
    auto select = [&](int index) {
      if (!vm.event("on_select", index)) {
        std::fprintf(stderr, "select %d: %s\n", index, vm.error());
        std::abort();
      }
    };
    package("calculator");
    select(13);
    select(16);
    select(14);
    select(20);
    assert(host.display(2) == "3" && host.scene.controlCount == 20);
    select(20);
    assert(host.display(2) == "5");
    select(1);
    select(13);
    select(4);
    select(17);
    select(20);
    assert(host.display(2) == "Error");
    select(1);
    select(13);
    select(16);
    select(14);
    select(20);
    select(20);
    host.preview("calculator");
    vm.close();
    host.storage.clear();
    package("woodfish");
    select(1);
    select(1);
    assert(host.display(1) == "2" && host.storage["count"] == "2");
    host.preview("woodfish");
    vm.close();
    package("woodfish");
    assert(host.display(1) == "2");
    select(2);
    select(5);
    assert(host.storage["count"] == "0");
    vm.close();
    host.storage.clear();
    host.storage["game"] = "0,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0";
    package("game2048");
    const auto oldGame = host.storage["game"];
    const auto writesBeforeMove = host.storageWrites;
    select(17);
    assert(host.display(0) == "8" && host.storage["game"] == oldGame);
    assert(host.storageWrites == writesBeforeMove);
    host.now += 1999;
    assert(vm.event("on_tick") && host.storageWrites == writesBeforeMove);
    host.now += 2;
    host.storageDelayMs = 250;
    assert(vm.event("on_tick"));
    host.storageDelayMs = 0;
    assert(host.storage["game"].find("8,4,4,") == 0);
    const auto writesAfterSave = host.storageWrites;
    host.now += 3000;
    assert(vm.event("on_tick") && host.storageWrites == writesAfterSave);
    vm.close();
    package("game2048");
    assert(host.display(0) == "8");
    host.preview("game2048");
    select(21);
    select(23);
    assert(vm.event("on_stop"));  // Home/sleep must flush before the VM is destroyed.
    vm.close();
    package("game2048");
    assert(host.display(0) == "0");
    const auto beforeContinuous = host.storageWrites;
    for (int i = 0; i < 24; ++i) {
      host.now += 500;
      select(17 + i % 4);
      assert(vm.event("on_tick"));
      if (i < 20) assert(host.storageWrites == beforeContinuous);
    }
    assert(host.storageWrites > beforeContinuous);
    select(21);
    select(23);
    host.failWrites = true;
    host.now += 2001;
    assert(vm.event("on_tick") && host.heading.find("Save failed") != std::string::npos);
    host.failWrites = false;
    host.now += 2001;
    assert(vm.event("on_tick"));
    assert(host.heading == "2048");
    assert(host.storage["game"].find("0,") == 0);
    assert(vm.event("on_stop"));
    vm.close();
    host.storage.clear();
    for (const char* id : {"gomoku", "sudoku", "minesweeper", "sokoban", "chinese_chess"}) {
      package(id);
      assert(host.scene.controlCount > 0);
      const auto region = host.scene.commands[host.scene.controls[0]];
      assert(vm.event("on_select", 1, "", region.x + region.width / 2, region.y + region.height / 2));
      for (int i = 0; i < 20; ++i) {
        if (!vm.event("on_tick")) {
          std::fprintf(stderr, "%s tick: %s\n", id, vm.error());
          std::abort();
        }
      }
      host.preview(id);
      std::printf("%s: %zu commands, %zu controls, VM peak %zu bytes\n", id, host.scene.count, host.scene.controlCount,
                  vm.peak());
      vm.close();
      host.storage.clear();
    }
    for (const char* device : {"288485447014", "b81f3fd1bf04", "000000000000", "ffffffffffff"}) {
      host.identity = device;
      host.storage.clear();
      package("buddy");
      select(1);
      select(2);
      HalSystem::DeviceId mac{};
      for (size_t i = 0; i < mac.size(); ++i)
        mac[i] = std::strtoul(host.identity.substr(i * 2, 2).c_str(), nullptr, 16);
      const auto original = buddy::generate(mac);
      assert(host.contains(original.name.data()));
      for (auto stat : original.stats) assert(host.contains(std::to_string(stat).c_str()));
      select(2);
      host.preview("buddy");
      vm.close();
    }
    for (const char* id :
         {"calculator", "woodfish", "game2048", "gomoku", "sudoku", "minesweeper", "sokoban", "chinese_chess", "buddy",
          "reading_stats", "avatar", "standby", "pixel_switch", "airpage", "weread"}) {
      for (const auto size :
           {std::make_pair(336, 430), std::make_pair(432, 640), std::make_pair(912, 420), std::make_pair(492, 850)}) {
        host.viewportWidth = size.first;
        host.viewportHeight = size.second;
        host.storage.clear();
        package(id);
        assert(vm.event("on_resize"));
        if (!std::strcmp(id, "reading_stats")) {
          host.preview(id);
          select(1);
          select(21);
          assert(vm.event("on_back"));
          select(5);
        }
        if (!std::strcmp(id, "buddy")) {
          select(1);
          select(2);
        }
        if (!std::strcmp(id, "standby")) {
          select(1);
          select(2);
          assert(vm.event("on_tick"));
        }
        if (!std::strcmp(id, "pixel_switch")) {
          select(1);
          assert(vm.event("on_tick"));
          select(20);
          assert(host.scene.textUsed > 6000 && host.scene.count < 40);
          host.preview("pixel_switch");
          assert(vm.event("on_swipe", 0, "left"));
          assert(host.contains("29, 50"));
          assert(vm.event("on_input", 0, "59,99"));
          assert(host.contains("59, 99"));
        }
        if (!std::strcmp(id, "airpage")) {
          assert(vm.event("on_tick"));
          host.preview(id);
          select(3);
          select(9);
          select(8);
          select(101);
          assert(vm.event("on_tick"));
          assert(vm.event("on_image"));
          select(4);
          select(5);
          select(1);
        }
        if (!std::strcmp(id, "weread")) {
          host.wereadState = "idle";
          ++host.serviceRevision;
          assert(vm.event("on_tick"));
          host.preview(id);
          select(4);
          select(3);
          select(101);
          select(13);
          select(41);
          select(40);
          select(42);
          select(14);
          select(15);
          select(11);
          host.wereadState = "range";
          ++host.serviceRevision;
          assert(vm.event("on_tick"));
          select(201);
          select(202);
          select(31);
          select(30);
          select(33);
          host.wereadState = "done";
          ++host.serviceRevision;
          assert(vm.event("on_tick"));
          select(7);
          select(1);
          host.wereadState = "qr";
          ++host.serviceRevision;
          assert(vm.event("on_tick"));
          select(900);
          host.wereadState = "disclaimer";
          ++host.serviceRevision;
          assert(vm.event("on_tick"));
          for (int page = 0; page < 16; ++page) select(81);
        }
        vm.close();
      }
    }
    host.viewportWidth = 432;
    host.viewportHeight = 640;
    host.useChinese = true;
    for (const char* id : {"weread", "airpage"}) {
      for (const auto size :
           {std::make_pair(336, 430), std::make_pair(432, 640), std::make_pair(912, 420), std::make_pair(492, 850)}) {
        host.viewportWidth = size.first;
        host.viewportHeight = size.second;
        host.wereadState = "idle";
        ++host.serviceRevision;
        package(id);
        assert(vm.event("on_tick"));
        if (!std::strcmp(id, "weread")) {
          select(101);
          select(13);
          select(41);
          select(42);
          select(7);
          host.wereadState = "disclaimer";
          ++host.serviceRevision;
          assert(vm.event("on_tick"));
          for (int page = 0; page < 32; ++page) select(81);
        } else {
          select(3);
          select(9);
          select(101);
          assert(vm.event("on_image"));
        }
        vm.close();
      }
    }
    host.useChinese = false;
    host.viewportWidth = 432;
    host.viewportHeight = 640;
    for (const char* id : {"rf_chat", "wifi_chat", "esp_chat"}) {
      host.storage.clear();
      package(id);
      std::printf("%s: VM peak %zu bytes\n", id, vm.peak());
      select(31);
      host.preview(id);
      select(12);
      select(15);
      select(16);
      assert(vm.event("on_back"));
      assert(vm.event("on_tick"));
      vm.close();
      host.stressChat = true;
      for (const auto size :
           {std::make_pair(336, 430), std::make_pair(432, 640), std::make_pair(912, 420), std::make_pair(492, 850)}) {
        host.viewportWidth = size.first;
        host.viewportHeight = size.second;
        package(id);
        select(31);
        select(20);
        select(21);
        select(101);
        select(23);
        select(12);
        select(11);
        select(24);
        assert(vm.event("on_back"));
        assert(vm.event("on_tick"));
        vm.close();
      }
      host.stressChat = false;
      host.viewportWidth = 432;
      host.viewportHeight = 640;
    }
    host.storage.clear();
    package("avatar");
    for (int i = 0; i < 50; ++i) select(1);
    host.preview("avatar");
    std::string svg;
    assert(
        crossstore::writeSvg(host.scene, host.viewportWidth, host.viewportHeight, [&](const char* text, size_t bytes) {
          svg.append(text, bytes);
          return true;
        }));
    assert(svg.find("<path") != std::string::npos && svg.find("<svg") != std::string::npos);
    assert(svg.find("Another face") == std::string::npos);
    assert(!crossstore::writeSvg(host.scene, host.viewportWidth, host.viewportHeight,
                                 [](const char*, size_t) { return false; }));
    vm.close();
    host.useChinese = true;
    for (const char* date :
         {"1900-01-31", "1984-02-02", "2000-02-29", "2025-07-25", "2026-02-17", "2026-10-23", "2100-12-31"}) {
      host.today = date;
      host.storage.clear();
      package("standby");
      select(1);
      std::tm now{};
      int year, month, day;
      assert(std::sscanf(date, "%d-%d-%d", &year, &month, &day) == 3);
      now.tm_year = year - 1900;
      now.tm_mon = month - 1;
      now.tm_mday = day;
      AlmanacDay original{};
      assert(computeAlmanac(now, original));
      const auto lunar = std::string("农历 ") + (original.lunarLeap ? "闰" : "") +
                         chinese_almanac::kLunarMonthNames[original.lunarMonth - 1] +
                         chinese_almanac::kLunarDayNames[original.lunarDay - 1] + "  " +
                         chinese_almanac::kStemNames[original.yearStemIdx] +
                         chinese_almanac::kBranchNames[original.yearBranchIdx] + "年";
      assert(host.contains(lunar.c_str()));
      const auto next = std::string("下个节气：") + chinese_almanac::kSolarTermNames[original.termNextIdx] + "  " +
                        std::to_string(original.daysToNextTerm) + "天";
      assert(host.contains(next.c_str()));
      host.preview("standby-calendar");
      vm.close();
    }
    host.useChinese = false;
    host.storage.clear();
    package("standby");
    host.preview("standby");
    vm.close();
    host.clockValid = false;
    package("standby");
    assert(host.contains("Set the clock to begin"));
    vm.close();
    host.clockValid = true;
    for (const char* id : {"library", "file_transfer", "opds"}) {
      vm.close();
      package(id);
      assert(host.route == id);
    }
    vm.close();
    assert(vm.used() == 0);
    std::puts("CrossStore packages: calculator, persisted woodfish/2048, core routes passed");
  }
}
