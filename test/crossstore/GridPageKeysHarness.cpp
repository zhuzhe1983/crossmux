#include <components/lists/list.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

#include "InxItemLayout.h"
#include "activities/MainTab.h"

namespace fui = freeink::ui;
namespace freeink::ui {
// SDK_HELPERS
}

#define LOG_DBG(...) ((void)0)
struct RenderLock {
  static inline bool held = false;
  RenderLock() {
    assert(!held);
    held = true;
  }
  ~RenderLock() { held = false; }
};
struct BoardConfig {
  static inline bool mono = true;
  static bool isPaperMono() { return mono; }
};
class MappedInputManager {
 public:
  enum class Button { Up, Down, Confirm, Back };
  enum class SwipeDir { None, Up, Down, Left, Right };
  std::array<bool, 4> pressed{}, held{}, released{};
  SwipeDir swipe = SwipeDir::None;
  bool touch = false;
  int touchSlot = 0;
  bool isPressed(Button button) const { return held[static_cast<size_t>(button)]; }
  bool wasPressed(Button button) const { return pressed[static_cast<size_t>(button)]; }
  bool wasReleased(Button button) const { return released[static_cast<size_t>(button)]; }
  SwipeDir wasSwipe(int* x, int* y) const {
    *x = *y = 10;
    return swipe;
  }
};

class UiListActivity {
 public:
  fui::ListNav nav;  // Real SDK atomics, pending scroll and viewport feedback.
  MappedInputManager mappedInput;
  int count = 1, redraws = 0;
  struct Manager {
    int focuses = 0;
    void focusMainTabContent() { ++focuses; }
  } activityManager;
  fui::ListNav& activeNav() { return nav; }
  int listCount() const { return count; }
  bool defersContentSelection() const { return true; }
  void requestUpdate() { ++redraws; }
  void syncPagedViewport(int pageItems, bool allowPartialPage = false);
  virtual bool handleListSwipe(MappedInputManager::SwipeDir swipe, int x, int y);
};

class AppsMenuActivity : public UiListActivity {
 public:
  bool icons = true, gridButtonReleasePending = false, selected = false, routing = true;
  int iconCacheStart = 0, iconLoads = 0, touches = 0, touchedIndex = -1, moves = 0;
  struct App {
    bool invalidated() const { return false; }
  } app;
  struct Touch {
    bool routed = false, dispatched = false;
    explicit operator bool() const { return dispatched; }
  };
  explicit AppsMenuActivity(int entries) { count = entries; }
  bool usesIconLayout() const { return icons; }
  int getVisibleAppCount() const { return count; }
  void clearContentSelection() { selected = false; }
  void closeRouting() { routing = false; }
  void loadPageIcons() {
    assert(RenderLock::held);
    ++iconLoads;
    iconCacheStart = nav.top;
  }
  void moveSelectionTo(int index) {
    ++moves;
    selected = true;
    nav.requestSelection(index);
  }
  Touch routeTouch(const MappedInputManager& input) {
    if (!routing || !input.touch) return {};
    const auto rect = InxGridGeometry::cellBounds(input.touchSlot, 432, 640);
    touchedIndex =
        InxGridGeometry::indexFromPoint(rect.x + rect.width / 2, rect.y + rect.height / 2, 432, 640, nav.top, count);
    if (touchedIndex < 0) return {true, false};
    ++touches;
    return {true, true};
  }
  void build() {
    // Font scan and final drawing repeat layout in production. Fixed pages
    // must remain stable through every pass, including the incomplete tail.
    syncPagedViewport(InxGridGeometry::itemsPerPage, BoardConfig::mono && icons);
    const int first = nav.top;
    for (int i = 0; i < 3; ++i) {
      syncPagedViewport(InxGridGeometry::itemsPerPage, BoardConfig::mono && icons);
      assert(nav.top == first && !nav.consumeRebuildNeeded());
    }
    routing = true;
  }
  void guardGridButtons();
  bool handleCustomInput();
  void turnIconPage(int direction);
  void selectMainTabContentEdge(MainTabContentEdge edge);
  bool handleListSwipe(MappedInputManager::SwipeDir swipe, int x, int y) override;
};

// PRODUCTION_METHODS

using Button = MappedInputManager::Button;
using Swipe = MappedInputManager::SwipeDir;
static constexpr int PAGE = InxGridGeometry::itemsPerPage;
static_assert(PAGE == 12);

static void gesture(AppsMenuActivity& app, Button button, int expectedTop) {
  const auto key = static_cast<size_t>(button);
  app.mappedInput = {};
  app.mappedInput.pressed[key] = app.mappedInput.held[key] = true;
  assert(app.handleCustomInput());
  app.build();
  assert(app.nav.top == expectedTop);
  const int loads = app.iconLoads, redraws = app.redraws, selection = app.nav.selected.load();
  app.mappedInput.pressed[key] = false;
  for (int i = 0; i < 100; ++i) {
    assert(app.handleCustomInput());
    app.build();
    assert(app.nav.top == expectedTop && app.nav.selected == selection);
  }
  app.mappedInput.held[key] = false;
  app.mappedInput.released[key] = true;
  assert(app.handleCustomInput());
  app.build();
  assert(app.nav.top == expectedTop && app.iconLoads == loads && app.redraws == redraws);
  app.mappedInput = {};
  assert(!app.handleCustomInput());
  assert(!app.gridButtonReleasePending);
}

static std::vector<int> visible(const AppsMenuActivity& app) {
  std::vector<int> result;
  for (int i = app.nav.top; i < std::min(app.count, app.nav.top + PAGE); ++i) result.push_back(i);
  return result;
}

int main() {
  for (int count : {1, 12, 13, 25, 65}) {
    AppsMenuActivity app(count);
    app.build();
    assert(app.nav.top == 0);
    app.nav.selected = std::min(5, count - 1);
    app.selected = true;
    const int selection = app.nav.selected.load();
    gesture(app, Button::Up, 0);
    assert(app.iconLoads == 0 && app.nav.selected == selection && app.selected);
    std::vector<int> seen = visible(app);
    const int last = (count - 1) / PAGE * PAGE;
    for (int top = PAGE; top <= last; top += PAGE) {
      gesture(app, Button::Down, top);
      assert(app.iconCacheStart == top && !app.selected && app.moves == 0);
      const auto page = visible(app);
      seen.insert(seen.end(), page.begin(), page.end());
    }
    assert(seen.size() == static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) assert(seen[i] == i);  // No repeats or omissions.
    const int tailSelection = app.nav.selected.load(), tailLoads = app.iconLoads;
    gesture(app, Button::Down, last);
    assert(app.nav.selected == tailSelection && app.iconLoads == tailLoads);
    for (int top = last - PAGE; top >= 0; top -= PAGE) gesture(app, Button::Up, top);
    assert(app.nav.top == 0);

    // Swipe paging uses full capacity even when the last page has 1/5 rows.
    for (int top = PAGE; top <= last; top += PAGE) {
      assert(app.handleListSwipe(Swipe::Up, 10, 10));
      app.build();
      assert(app.nav.top == top);
    }
    assert(app.handleListSwipe(Swipe::Up, 10, 10));
    app.build();
    assert(app.nav.top == last);
    for (int top = last - PAGE; top >= 0; top -= PAGE) {
      assert(app.handleListSwipe(Swipe::Down, 10, 10));
      app.build();
      assert(app.nav.top == top);
    }
    assert(!app.handleListSwipe(Swipe::Left, 10, 10));

    // ActivityManager dispatches this method once from tab focus, consuming
    // the entering key gesture itself. It must page rather than pick a row.
    app.selectMainTabContentEdge(MainTabContentEdge::First);
    app.build();
    assert(app.nav.top == std::min(PAGE, last) && app.moves == 0);
    app.selectMainTabContentEdge(MainTabContentEdge::Last);
    app.build();
    assert(app.nav.top == 0 && app.moves == 0);
  }

  for (Button button : {Button::Up, Button::Down}) {
    AppsMenuActivity app(25);
    app.turnIconPage(1);
    app.build();
    const auto key = static_cast<size_t>(button);
    app.mappedInput.held[key] = true;
    app.guardGridButtons();  // Returning from a child with a key still held.
    for (int i = 0; i < 100; ++i) assert(app.handleCustomInput());
    assert(app.nav.top == 12);
    app.mappedInput.touch = true;
    app.mappedInput.touchSlot = 3;
    assert(app.handleCustomInput());
    assert(app.touches == 1 && app.touchedIndex == 15 && app.nav.top == 12);
    app.mappedInput.touch = false;
    app.mappedInput.swipe = Swipe::Up;
    assert(app.handleCustomInput());
    app.build();
    assert(app.nav.top == 24 && app.gridButtonReleasePending);
    app.mappedInput.swipe = Swipe::None;
    app.mappedInput.held[key] = false;
    app.mappedInput.released[key] = true;
    assert(app.handleCustomInput());
    app.build();
    assert(app.nav.top == 24);
    app.mappedInput = {};
    assert(!app.handleCustomInput());
    gesture(app, Button::Up, 12);
  }

  // A simultaneous pair consumes the gesture without choosing a direction.
  {
    AppsMenuActivity app(25);
    app.build();
    app.mappedInput.pressed[0] = app.mappedInput.pressed[1] = true;
    app.mappedInput.held[0] = app.mappedInput.held[1] = true;
    assert(app.handleCustomInput());
    assert(app.nav.top == 0 && app.iconLoads == 0);
    app.mappedInput.pressed = {};
    app.mappedInput.held = {};
    app.mappedInput.released[0] = app.mappedInput.released[1] = true;
    assert(app.handleCustomInput());
    assert(app.nav.top == 0);
  }

  for (bool mono : {false, true}) {
    for (bool icons : {false, true}) {
      if (mono && icons) continue;
      BoardConfig::mono = mono;
      AppsMenuActivity app(25);
      app.icons = icons;
      app.build();
      app.mappedInput.pressed[1] = app.mappedInput.held[1] = true;
      assert(!app.handleCustomInput());  // Existing default input owner keeps it.
      assert(app.nav.top == 0 && app.iconLoads == 0);
      app.mappedInput = {};
      app.selectMainTabContentEdge(MainTabContentEdge::Last);
      app.build();
      assert(app.moves == 1 && app.nav.selected == 24 && app.nav.top == 13);
      app.selectMainTabContentEdge(MainTabContentEdge::First);
      app.build();
      assert(app.moves == 2 && app.nav.selected == 0 && app.nav.top == 0);
      assert(app.handleListSwipe(Swipe::Up, 10, 10));
      app.build();
      assert(app.nav.top == 12 && app.iconLoads == 0);
      assert(app.handleListSwipe(Swipe::Up, 10, 10));
      app.build();
      assert(app.nav.top == 13);  // Legacy full-tail list behavior is unchanged.
    }
  }
  BoardConfig::mono = true;
  {
    // Inherited Confirm/Back are not claimed by the grid's navigation hook.
    AppsMenuActivity app(25);
    app.build();
    for (Button button : {Button::Confirm, Button::Back}) {
      const auto key = static_cast<size_t>(button);
      app.mappedInput.pressed[key] = app.mappedInput.held[key] = true;
      app.guardGridButtons();
      assert(!app.gridButtonReleasePending && !app.handleCustomInput());
      app.mappedInput = {};
      app.mappedInput.released[key] = true;
      assert(!app.handleCustomInput());
      app.mappedInput = {};
    }
  }
  {
    AppsMenuActivity app(65);
    app.turnIconPage(5);
    app.build();
    assert(app.nav.top == 60 && app.nav.selected == 60);
    app.count = 13;
    app.build();
    assert(app.nav.top == 12 && app.nav.selected == 12);
    app.count = 1;
    app.build();
    assert(app.nav.top == 0 && app.nav.selected == 0);
  }
  std::puts(
      "Production grid keys + SDK ListNav: 12-slot pages, complete gestures, inherited holds, touch/swipe, tab entry "
      "and legacy paths passed");
}
