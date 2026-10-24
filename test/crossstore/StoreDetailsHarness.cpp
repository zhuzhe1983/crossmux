#include <FreeInkApp.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "crossstore/StoreDetails.h"

namespace fui = freeink::ui;
struct RenderLock {
  static inline bool held = false;
  RenderLock() {
    assert(!held);
    held = true;
  }
  ~RenderLock() { held = false; }
};
struct UITheme {
  struct Metrics {
    int contentSidePadding = 20, listRowHeight = 30;
  } metrics;
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  const Metrics& getMetrics() const { return metrics; }
};
class AppStoreActivity {
 public:
  using UiScreen = fui::Screen<24>;
  enum class View { Browser, Details };
  static constexpr fui::ActionId ACTION_DETAIL = 4;
  struct Renderer {
    int width = 432, height = 640;
    int getScreenWidth() const { return width; }
    int getScreenHeight() const { return height; }
  } renderer;
  struct App {
    void clearTapFlash() {}
  } app;
  Rect bounds{12, 60, 408, 520};
  View view_ = View::Details;
  fui::ListNav nav_;
  fui::ListItem rows_[20]{};
  char labels_[20][96]{};
  size_t rowCount_ = 8;
  int primaryRow_ = 7;
  bool primaryFocused_ = true, keyboardFocus_ = false;
  int calls = 0, activated = -1, updates = 0;
  AppStoreActivity() {
    for (size_t i = 0; i < rowCount_; ++i) {
      std::snprintf(labels_[i], sizeof(labels_[i]), i == 7 ? "Install" : "Row %u", static_cast<unsigned>(i));
      rows_[i].label = labels_[i];
      rows_[i].actionValue = static_cast<int16_t>(i);
      rows_[i].enabled = i >= 5;
    }
    nav_.selected = -1;
    nav_.followOnBuild = false;
  }
  Rect listBounds() const { return bounds; }
  void requestUpdate() { ++updates; }
  void activateIndex(int index) {
    assert(!RenderLock::held);
    ++calls;
    activated = index;
  }
  size_t detailListCount() const;
  static void detailCallback(const fui::ActionEvent&, void*);
  void stepDetailsFocus(int);
  void activateDetailsFocus();
  void scrollDetails(int);
  void buildDetails(UiScreen&);
};

// PRODUCTION_DETAILS

struct RecordingTarget : fui::DrawTarget {
  struct Text {
    fui::Rect rect;
    std::string value;
    fui::Color color;
  };
  struct Fill {
    fui::Rect rect;
    fui::Paint paint;
  };
  int16_t lineSize = 24;
  std::vector<Text> texts;
  std::vector<Fill> fills;
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * lineSize / 2), lineSize};
  }
  int16_t lineHeight(fui::FontId) const override { return lineSize; }
  void fill(fui::Rect r, fui::Paint p, uint8_t, uint8_t) override { fills.push_back({r, p}); }
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect r, const char* text, fui::TextStyle style) override { texts.push_back({r, text, style.color}); }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};

bool same(const fui::Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
bool contains(const Rect& outer, const Rect& inner) {
  return inner.width > 0 && inner.height > 0 && inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width && inner.y + inner.height <= outer.y + outer.height;
}

void checkNavigation() {
  AppStoreActivity store;
  store.activateDetailsFocus();
  assert(store.calls == 1 && store.activated == 7);
  store.scrollDetails(4);
  assert(store.primaryFocused_ && store.nav_.selected == -1);
  // Both directions reach the primary control and every secondary action.
  for (int direction : {-1, 1}) {
    store.primaryFocused_ = true;
    std::array<bool, 8> visited{};
    for (int step = 0; step < 8; ++step) {
      store.stepDetailsFocus(direction);
      const int index = store.primaryFocused_ ? store.primaryRow_ : store.nav_.selected.load();
      visited[index] = true;
      const int before = store.calls;
      store.activateDetailsFocus();
      assert(store.calls == before + (index >= 5 ? 1 : 0));
    }
    assert(std::all_of(visited.begin(), visited.end(), [](bool value) { return value; }));
    assert(store.primaryFocused_ && store.nav_.selected == -1);
  }
  fui::ActionEvent event;
  event.action = AppStoreActivity::ACTION_DETAIL;
  event.value = 0;
  const int before = store.calls;
  AppStoreActivity::detailCallback(event, &store);
  assert(store.calls == before);
  event.value = 6;
  AppStoreActivity::detailCallback(event, &store);
  assert(store.calls == before + 1 && store.activated == 6 && !store.primaryFocused_);
  event.value = 7;
  AppStoreActivity::detailCallback(event, &store);
  assert(store.calls == before + 2 && store.activated == 7 && store.primaryFocused_);
  store.view_ = AppStoreActivity::View::Browser;
  AppStoreActivity::detailCallback(event, &store);
  assert(store.calls == before + 2);
  // Incompatible/uninstalled packages can have only information, no primary.
  store.view_ = AppStoreActivity::View::Details;
  store.primaryRow_ = -1;
  store.primaryFocused_ = false;
  store.rowCount_ = 5;
  for (int step = 0; step < 10; ++step) {
    store.stepDetailsFocus(1);
    store.activateDetailsFocus();
  }
  assert(store.calls == before + 2);
}

void checkRendering() {
  const std::array<Rect, 4> sizes{Rect{13, 60, 336, 340}, Rect{13, 60, 432, 550}, Rect{13, 60, 912, 330},
                                  Rect{13, 60, 492, 760}};
  for (const auto bounds : sizes)
    for (int16_t line : {24, 29, 40})
      for (int16_t minimum : {44, 72, 96}) {
        AppStoreActivity store;
        store.bounds = bounds;
        store.renderer.width = bounds.x + bounds.width + 13;
        store.renderer.height = bounds.y + bounds.height + 30;
        RecordingTarget target;
        target.lineSize = line;
        fui::DeviceContext device;
        device.width = store.renderer.width;
        device.height = store.renderer.height;
        device.minTouchSize = minimum;
        fui::ThemeTokens theme;
        theme.minTouchSize = minimum;
        fui::InputSnapshot input;
        fui::InteractionBuffer<24> interactions;
        fui::Frame<24> frame(target, device, input, interactions);
        AppStoreActivity::UiScreen screen(frame, theme);
        store.buildDetails(screen);
        const auto layout = crossstore::StoreDetails::fit(bounds, line, minimum, 20, true);
        assert(contains(bounds, layout.list) && contains(bounds, layout.primary));
        assert(layout.list.y + layout.list.height + 8 <= layout.primary.y);
        assert(layout.primary.height >= 44 && layout.primary.width >= 44);
        int primaryHits = 0;
        for (size_t i = 0; i < interactions.count(); ++i) {
          const auto& hit = interactions.data()[i];
          assert(hit.rect.height >= 44 && hit.value >= 5);  // explanatory rows have no hits
          if (hit.value == 7) {
            ++primaryHits;
            assert(same(hit.rect, layout.primary));
          } else
            assert(hit.rect.y + hit.rect.height + 8 <= layout.primary.y);
          for (size_t j = 0; j < i; ++j) {
            const auto& other = interactions.data()[j].rect;
            assert(hit.rect.y + hit.rect.height + 8 <= other.y || other.y + other.height + 8 <= hit.rect.y);
          }
        }
        assert(primaryHits == 1 && !interactions.overflowed());
        int labels = 0;
        for (const auto& text : target.texts)
          if (text.value == "Install") {
            ++labels;
            assert(text.color == fui::Color::White);
          }
        assert(labels == 1);
        bool solid = false;
        for (const auto& fill : target.fills)
          if (same(fill.rect, layout.primary))
            solid = fill.paint.kind == fui::PaintKind::Solid && fill.paint.color == fui::Color::Black;
        assert(solid);
        // Scrolling to secondary actions must not scroll away/duplicate the CTA.
        store.scrollDetails(20);
        target.texts.clear();
        target.fills.clear();
        fui::Frame<24> nextFrame(target, device, input, interactions);
        AppStoreActivity::UiScreen next(nextFrame, theme);
        store.buildDetails(next);
        primaryHits = 0;
        for (size_t i = 0; i < interactions.count(); ++i)
          if (interactions.data()[i].value == 7) {
            ++primaryHits;
            assert(same(interactions.data()[i].rect, layout.primary));
          }
        assert(primaryHits == 1);
        assert(store.nav_.drawnCount == static_cast<int>(store.detailListCount()));
      }
}
int main() {
  checkNavigation();
  checkRendering();
  std::puts("Production details: fixed primary draw/hit geometry, read-only information, scroll and key access passed");
}
