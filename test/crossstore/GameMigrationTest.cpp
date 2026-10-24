#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "CrossStoreRuntime.h"

class GameHost : public crossstore::Host {
 public:
  std::string path, heading;
  std::map<std::string, std::string> files;
  crossstore::Canvas scene;
  unsigned now = 0, writes = 0;
  bool zh = false, fail = false, exited = false;
  int width = 432, height = 640;
  void title(const char* s) override { heading = s; }
  void clear() override { scene.clear(); }
  void grid(unsigned) override {}
  void caption(const char*) override {}
  bool row(const char*, const char*) override { return true; }
  bool capability(const char*) const override { return true; }
  bool chinese() const override { return zh; }
  uint32_t milliseconds() const override { return now; }
  bool read(const char* key, char* out, size_t cap) override {
    auto it = files.find(key);
    if (it == files.end() || it->second.size() >= cap) return false;
    std::memcpy(out, it->second.c_str(), it->second.size() + 1);
    return true;
  }
  bool write(const char* key, const char* value, size_t bytes) override {
    ++writes;
    assert(bytes <= 2048);
    if (fail) return false;
    files[key].assign(value, bytes);
    return true;
  }
  void input(const char*, const char*) override {}
  void notify(const char*) override {}
  bool open(const char*) override { return false; }
  void exit() override { exited = true; }
  void size(int& w, int& h) const override {
    w = width;
    h = height;
  }
  bool canvas() override { return true; }
  bool draw(const crossstore::DrawCommand& c, const char* label) override {
    if (c.kind == crossstore::DrawKind::Button || c.kind == crossstore::DrawKind::Hit) {
      assert(c.x >= 0 && c.y >= 0 && c.x + c.width <= width && c.y + c.height <= height);
      for (size_t i = 0; i < scene.controlCount; ++i) {
        const auto& o = scene.commands[scene.controls[i]];
        const int dx = std::max<int>(c.x - o.x - o.width, o.x - c.x - c.width);
        const int dy = std::max<int>(c.y - o.y - o.height, o.y - c.y - c.height);
        assert(dx >= 6 || dy >= 6);
      }
    }
    return scene.add(c, label);
  }
  bool asset(const char* name, size_t offset, char* out, size_t cap, size_t& bytes) override {
    std::ifstream in(path + "/" + name, std::ios::binary);
    if (!in) return false;
    in.seekg(offset);
    in.read(out, cap);
    bytes = in.gcount();
    return true;
  }
  crossstore::DrawCommand control(int id) const {
    for (size_t i = 0; i < scene.controlCount; ++i) {
      const auto& c = scene.commands[scene.controls[i]];
      if (c.action == id) return c;
    }
    std::fprintf(stderr, "missing control %d: %s\n", id, heading.c_str());
    std::abort();
  }
};
static void event(crossstore::Runtime& vm, const char* name, int id = 0, int x = -1, int y = -1) {
  if (!vm.event(name, id, "", x, y)) {
    std::fprintf(stderr, "%s(%d): %s (peak %zu used %zu)\n", name, id, vm.error(), vm.peak(), vm.used());
    std::abort();
  }
}
static void select(crossstore::Runtime& vm, int id) { event(vm, "on_select", id); }
static void load(GameHost& h, crossstore::Runtime& vm, const std::string& root, const char* id) {
  h.path = root + "/" + id;
  std::ifstream in(h.path + "/main.lua");
  std::string source{std::istreambuf_iterator<char>(in), {}};
  if (!vm.load(source.data(), source.size())) {
    std::fprintf(stderr, "%s load: %s, save %zu, peak %zu used %zu\n", id, vm.error(), h.files["game"].size(),
                 vm.peak(), vm.used());
    std::abort();
  }
}
static std::vector<int> numbers(const std::string& data) {
  std::vector<int> out;
  auto pipe = data.find('|');
  if (pipe != std::string::npos) {
    out = numbers(data.substr(0, pipe));
    size_t start = pipe + 1;
    for (int block = 0; block < 4; ++block) {
      auto end = data.find('|', start);
      auto part = data.substr(start, end - start);
      if (block < 3) {
        for (char c : part) out.push_back(c - '0');
      } else {
        auto tail = numbers(part);
        out.insert(out.end(), tail.begin(), tail.end());
      }
      start = end + 1;
    }
    return out;
  }
  std::stringstream in(data);
  std::string s;
  while (std::getline(in, s, ','))
    if (!s.empty()) out.push_back(std::stoi(s));
  return out;
}
static std::string csv(const std::vector<int>& values) {
  std::string s;
  for (auto v : values) {
    if (!s.empty()) s += ",";
    s += std::to_string(v);
  }
  return s;
}
static void sudoku_cell(GameHost& h, crossstore::Runtime& vm, int i) {
  auto r = h.control(1);
  event(vm, "on_select", 1, r.x + ((i % 9) * 2 + 1) * r.width / 18, r.y + ((i / 9) * 2 + 1) * r.height / 18);
}
static void gomoku_cell(GameHost& h, crossstore::Runtime& vm, int n, int i) {
  auto r = h.control(1);
  double margin = std::max(15., double(r.width) / (n + 1)), step = (r.width - 2 * margin) / (n - 1);
  event(vm, "on_select", 1, int(r.x + margin + (i % n) * step + .5), int(r.y + margin + (i / n) * step + .5));
}
static void sudoku(const std::string& root) {
  GameHost h;
  crossstore::Runtime vm(h);
  load(h, vm, root, "sudoku");
  assert(h.writes == 0);
  event(vm, "on_stop");
  auto state = numbers(h.files["game"]);
  assert(state.size() == 341);
  assert(state[0] == 3 && state[1] == 1);
  for (int difficulty = 1; difficulty <= 3; ++difficulty) {
    select(vm, 22);
    select(vm, 25);
    select(vm, 29 + difficulty);
    event(vm, "on_stop");
    state = numbers(h.files["game"]);
    assert(state[1] == difficulty);
    int clues = 0;
    for (int i = 0; i < 81; ++i) clues += state[17 + i] > 0;
    assert(clues == (difficulty == 1 ? 43 : difficulty == 2 ? 35 : 29));
  }
  int blank = 0;
  while (state[17 + blank]) ++blank;
  sudoku_cell(h, vm, blank);
  unsigned before = h.writes;
  select(vm, 20);
  select(vm, 2);
  assert(h.writes == before);  // Notes remain in RAM.
  h.now += 1000;
  event(vm, "on_tick");
  assert(h.writes == before);
  h.now += 1001;
  event(vm, "on_tick");
  assert(h.writes == before + 1);
  state = numbers(h.files["game"]);
  assert(state[17 + 243 + blank] == 1);
  select(vm, 21);
  select(vm, 20);  // Undo note and leave notes mode.
  state = numbers(h.files["game"]);
  int correct = state[17 + 162 + blank];
  select(vm, correct % 9 + 2);  // A different digit increments mistakes.
  event(vm, "on_stop");
  state = numbers(h.files["game"]);
  assert(state[2] == 1);
  for (int i = 0; i < 4; ++i) {
    select(vm, 22);
    select(vm, 34);
  }
  event(vm, "on_stop");
  state = numbers(h.files["game"]);
  assert(state[3] == 0);
  // Three hints only, and solving counts once even after another start/stop.
  for (int i = 0; i < 81; ++i)
    if (state[17 + i] == 0) {
      sudoku_cell(h, vm, i);
      select(vm, state[17 + 162 + i] + 1);
    }
  event(vm, "on_stop");
  state = numbers(h.files["game"]);
  assert(state[7] == 1 && state[13] == 1);
  std::string solved = h.files["game"];
  vm.close();
  load(h, vm, root, "sudoku");
  event(vm, "on_stop");
  assert(h.files["game"] == solved);
  // New game remains intact through a failed write; Back refuses to drop it.
  select(vm, 22);
  select(vm, 25);
  select(vm, 30);
  h.fail = true;
  event(vm, "on_back");
  assert(!h.exited);
  assert(h.heading.find("Save failed") != std::string::npos);
  h.fail = false;
  h.now += 2001;
  event(vm, "on_tick");
  assert(h.heading == "Sudoku");
  assert(numbers(h.files["game"])[1] == 1);
  // Legacy 324-number saves are loaded, not replaced with a new puzzle.
  auto modern = numbers(h.files["game"]);
  std::vector<int> legacy(modern.begin() + 17, modern.end());
  vm.close();
  h.files["game"] = csv(legacy) + ",";
  load(h, vm, root, "sudoku");
  event(vm, "on_stop");
  auto migrated = numbers(h.files["game"]);
  assert(std::vector<int>(migrated.begin() + 17, migrated.end()) == legacy);
  // Maximum note masks in every editable cell and the entire 20-entry undo ring.
  auto dense = numbers(h.files["game"]);
  for (int i = 0; i < 81; ++i)
    if (dense[17 + i] == 0) {
      dense[17 + 81 + i] = 0;
      dense[17 + 243 + i] = 511;
    }
  std::vector<int> denseLegacy(dense.begin() + 17, dense.end());
  vm.close();
  h.files["game"] = csv(denseLegacy) + ",";
  load(h, vm, root, "sudoku");
  blank = 0;
  while (dense[17 + blank]) ++blank;
  sudoku_cell(h, vm, blank);
  select(vm, 20);
  for (int i = 0; i < 20; ++i) select(vm, i % 9 + 2);
  event(vm, "on_stop");
  for (int i = 0; i < 20; ++i) select(vm, 21);
  event(vm, "on_stop");
  auto undone = numbers(h.files["game"]);
  assert(undone[17 + 243 + blank] == 511);
  std::printf("sudoku: VM peak %zu / 65536 bytes, save %zu bytes\n", vm.peak(), h.files["game"].size());
}
static std::vector<int> gomoku_state(int level, const std::vector<int>& moves) {
  std::vector<int> out = {3, 9, 1, level, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  out.insert(out.end(), moves.begin(), moves.end());
  return out;
}
static void gomoku(const std::string& root) {
  GameHost h;
  crossstore::Runtime vm(h);
  load(h, vm, root, "gomoku");
  assert(h.writes == 0);
  // Option edits are drafts. Returning must preserve the active board.
  gomoku_cell(h, vm, 9, 40);
  event(vm, "on_stop");
  auto before = numbers(h.files["game"]);
  assert(before.size() == 18);
  select(vm, 8);
  select(vm, 22);
  select(vm, 23);
  select(vm, 25);
  select(vm, 26);
  event(vm, "on_back");
  event(vm, "on_stop");
  assert(numbers(h.files["game"]) == before);
  // Confirm a 15x15 two-player game, then exercise batched writes and reload.
  select(vm, 8);
  select(vm, 22);
  select(vm, 23);
  select(vm, 24);
  select(vm, 21);
  unsigned count = h.writes;
  gomoku_cell(h, vm, 15, 112);
  gomoku_cell(h, vm, 15, 113);
  assert(h.writes == count);
  h.now += 1999;
  event(vm, "on_tick");
  assert(h.writes == count);
  h.now += 2;
  event(vm, "on_tick");
  assert(h.writes == count + 1);
  auto saved = h.files["game"];
  vm.close();
  load(h, vm, root, "gomoku");
  event(vm, "on_stop");
  assert(h.files["game"] == saved);
  select(vm, 6);
  event(vm, "on_stop");
  assert(numbers(h.files["game"]).size() == 18);
  // Every level must block an immediate loss and take an immediate win.
  for (int level = 1; level <= 3; ++level)
    for (bool win : {false, true}) {
      vm.close();
      h.files["game"] = csv(gomoku_state(
          level, win ? std::vector<int>{6, 1, 8, 2, 10, 3, 12, 4, 14} : std::vector<int>{1, 19, 2, 21, 3, 23, 4}));
      load(h, vm, root, "gomoku");
      for (int tick = 0; tick < 40; ++tick) event(vm, "on_tick");
      event(vm, "on_stop");
      auto result = numbers(h.files["game"]);
      assert(result.back() == 5);
      assert(result.size() == (win ? 27 : 25));
      if (win) {
        assert(result[6] == 1 && result[9] == 1);
        auto data = h.files["game"];
        vm.close();
        load(h, vm, root, "gomoku");
        event(vm, "on_stop");
        assert(h.files["game"] == data);
      }
    }
  // Hard's sliced reply search completes and leaves no temporary stones in saves.
  vm.close();
  h.files["game"] = csv(gomoku_state(3, {41}));
  load(h, vm, root, "gomoku");
  for (int tick = 0; tick < 80; ++tick) {
    event(vm, "on_tick");
    event(vm, "on_stop");
    assert(numbers(h.files["game"]).size() <= 19);
  }
  assert(numbers(h.files["game"]).size() == 19);
  // Legacy moves, including turn ownership, survive the new combined record.
  vm.close();
  h.files["game"] = "9,0,41,42,43";
  load(h, vm, root, "gomoku");
  event(vm, "on_stop");
  auto old = numbers(h.files["game"]);
  assert(std::vector<int>(old.begin() + 17, old.end()) == std::vector<int>({41, 42, 43}));
  // A full 15x15 draw has no five-in-a-row in any direction. Loading, drawing
  // and saving the largest legal board must stay inside the same VM budget.
  std::vector<int> black, white;
  for (int r = 0; r < 15; ++r)
    for (int c = 0; c < 15; ++c) ((r + 2 * c) % 4 < 2 ? black : white).push_back(r * 15 + c + 1);
  assert(black.size() == 113 && white.size() == 112);
  auto full = gomoku_state(3, {});
  full[1] = 15;
  full[2] = 0;
  full[6] = 1;
  full[15] = 1;
  for (size_t i = 0; i < black.size(); ++i) {
    full.push_back(black[i]);
    if (i < white.size()) full.push_back(white[i]);
  }
  vm.close();
  h.files["game"] = csv(full);
  load(h, vm, root, "gomoku");
  event(vm, "on_stop");
  assert(numbers(h.files["game"]).size() == 242);
  std::printf("gomoku: VM peak %zu / 65536 bytes, save %zu bytes\n", vm.peak(), h.files["game"].size());
}
static void layouts(const std::string& root) {
  for (const char* id : {"sudoku", "gomoku"})
    for (bool zh : {false, true})
      for (auto size : {std::pair{336, 430}, std::pair{432, 640}, std::pair{912, 420}, std::pair{492, 850}}) {
        GameHost h;
        h.zh = zh;
        h.width = size.first;
        h.height = size.second;
        crossstore::Runtime vm(h);
        load(h, vm, root, id);
        event(vm, "on_resize");
        event(vm, "on_stop");
        if (!std::strcmp(id, "sudoku")) {
          select(vm, 22);
          select(vm, 33);
          select(vm, 24);
          select(vm, 22);
          select(vm, 35);
          select(vm, 24);
          select(vm, 22);
          select(vm, 25);
          select(vm, 32);
          select(vm, 22);
          select(vm, 34);
          select(vm, 20);
        } else {
          select(vm, 8);
          select(vm, 27);
          select(vm, 20);
          select(vm, 8);
          select(vm, 22);
          select(vm, 25);
          select(vm, 26);
          select(vm, 24);
          select(vm, 21);
          for (int i = 0; i < 30; ++i) event(vm, "on_tick");
        }
        event(vm, "on_stop");
        assert(vm.peak() <= 65536);
        std::printf("%s %s %dx%d: peak %zu, %zu draw commands\n", id, zh ? "zh" : "en", h.width, h.height, vm.peak(),
                    h.scene.count);
      }
}
int main(int argc, char** argv) {
  assert(argc == 2);
  sudoku(argv[1]);
  gomoku(argv[1]);
  layouts(argv[1]);
  puts("Game migration checks passed");
}
