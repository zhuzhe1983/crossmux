#include <FreeInkApp.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "crossstore/StoreConfirmation.h"

namespace fui = freeink::ui;
#define tr(key) translation(#key)
const char* translation(const char* key) {
  if (!std::strcmp(key, "STR_STORE_BYTES_FORMAT")) return "%u KiB (%u bytes)";
  return key;
}
struct I18nStub {
  const char* get(const char* value) const { return value; }
} I18N;
namespace crossstore {
struct Permission {
  unsigned bit;
  const char* title;
};
constexpr Permission PERMISSIONS[]{{1, "WiFi"}, {2, "Audio"}, {4, "Storage"}};
inline int removals = 0;
inline bool includeData = false, removalSucceeds = true;
inline std::string removedId;
bool uninstallPackage(const char* id, bool data) {
  ++removals;
  removedId = id;
  includeData = data;
  return removalSucceeds;
}
}  // namespace crossstore
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
constexpr int WL_CONNECTED = 1, WIFI_OFF = 0;
struct WifiStub {
  int connected = WL_CONNECTED, mode = 1;
  int status() const { return connected; }
  int getMode() const { return mode; }
} WiFi;
class WifiSelectionActivity {};
struct ActivityResult {
  bool isCancelled = false;
};
namespace BoardConfig {
bool mono = false;
bool isPaperMono() { return mono; }
}  // namespace BoardConfig
class MappedInputManager {
 public:
  enum class Button { Confirm, Back, NavNext, NavPrevious, Up, Down };
  enum class SwipeDir { None, Left, Right, Up, Down };
  std::array<bool, 6> held{}, pressed{}, released{};
  bool touchPressed = false, touchHeld = false, touchReleased = false;
  int touchAction = -1;
  SwipeDir swipe = SwipeDir::None;
  static bool sample(const std::array<bool, 6>& values, Button key) {
    // Portrait logical navigation aliases the side keys in MappedInputManager.
    return values[static_cast<size_t>(key)] || (key == Button::NavNext && values[static_cast<size_t>(Button::Down)]) ||
           (key == Button::NavPrevious && values[static_cast<size_t>(Button::Up)]);
  }
  bool isPressed(Button key) const { return sample(held, key); }
  bool wasPressed(Button key) const { return sample(pressed, key); }
  bool wasReleased(Button key) const { return sample(released, key); }
  bool wasScreenTouchPressed(int&, int&) const { return touchPressed; }
  bool isScreenTouchHeld(int&, int&) const { return touchHeld; }
  bool wasScreenTouchReleased() const { return touchReleased; }
  SwipeDir wasSwipe() const { return swipe; }
};
class AppStoreActivity {
 public:
  using UiScreen = fui::Screen<24>;
  enum class View {
    Browser,
    Details,
    Settings,
    Permissions,
    PermissionDetails,
    Capabilities,
    Busy,
    Error,
    ConfirmInstall,
    ConfirmRemove
  };
  enum class Control { All, Installed, Settings, Previous, Next, Refresh };
  static constexpr fui::ActionId ACTION_CONFIRM = 5;
  struct Renderer {
    int width = 432, height = 640;
    int getScreenWidth() const { return width; }
    int getScreenHeight() const { return height; }
    void drawRoundedRect(int, int, int, int, int, int, bool) {}
  } renderer;
  struct Entry {
    struct Package {
      char id[32] = "test_app", name[96] = "Application", version[24] = "1.2.3";
      unsigned permissions = 5;
    } package;
    unsigned bytes = 2050;
  } entries_[1];
  struct App {
    bool invalidated() const { return false; }
  } app;
  MappedInputManager mappedInput;
  struct Buttons {
    MappedInputManager& input;
    template <typename F>
    void onNextRelease(F fn) {
      if (input.wasReleased(MappedInputManager::Button::NavNext)) fn();
    }
    template <typename F>
    void onPreviousRelease(F fn) {
      if (input.wasReleased(MappedInputManager::Button::NavPrevious)) fn();
    }
  } buttons_{mappedInput};
  struct Shape {
    int capacity = 16;
  };
  struct Touch {
    bool routed = false, dispatched = false;
    explicit operator bool() const { return dispatched; }
  };
  Rect bounds{12, 60, 408, 520};
  View view_ = View::Details;
  fui::ListNav nav_;
  fui::ListItem rows_[20]{};
  char labels_[20][96]{}, subtitles_[20][192]{};
  size_t rowCount_ = 0, chosen_ = 0, count_ = 1, pageSize_ = 16;
  int focus_ = 0;
  bool primaryFocused_ = false, keyboardFocus_ = false, releasePending_ = false, wifiStarted_ = false;
  int installations = 0, grants = 0, detailsCalls = 0, errors = 0, reloads = 0, updates = 0, stops = 0, childStarts = 0;
  int pageTurns = 0;
  std::function<void(const ActivityResult&)> childResult;
  Rect listBounds() const { return bounds; }
  void resetRows(View view) {
    view_ = view;
    rowCount_ = 0;
    nav_.reset();
  }
  void addRow(const char* label, const char* subtitle = "") {
    assert(rowCount_ < 20);
    std::snprintf(labels_[rowCount_], sizeof(labels_[0]), "%s", label);
    std::snprintf(subtitles_[rowCount_], sizeof(subtitles_[0]), "%s", subtitle);
    rows_[rowCount_] = {};
    rows_[rowCount_].label = labels_[rowCount_];
    rows_[rowCount_].subtitle = subtitles_[rowCount_];
    ++rowCount_;
  }
  void requestUpdate() { ++updates; }
  void details(size_t) {
    assert(!RenderLock::held);
    ++detailsCalls;
    view_ = View::Details;
  }
  void install() {
    assert(!RenderLock::held);
    ++installations;
    ++grants;
    view_ = View::Busy;
  }
  void stopRefresh() {
    assert(!RenderLock::held);
    ++stops;
  }
  void reloadInstalled() { ++reloads; }
  void fail(const char*) {
    ++errors;
    view_ = View::Error;
  }
  template <typename Activity, typename F>
  void startActivityForResultWith(F fn) {
    assert(!RenderLock::held);
    ++childStarts;
    childResult = fn;
  }
  Shape geometry() const { return {}; }
  void pumpRefresh() {}
  void browser(bool = false) { view_ = View::Browser; }
  void turnPage(int) { ++pageTurns; }
  void scrollDetails(int rows) { nav_.requestScroll(rows); }
  void stepFocus(int direction) { stepConfirmationFocus(direction); }
  void activateDetailsFocus() {}
  void activateIndex(int) {}
  void control(Control) {}
  void finish() {}
  void permissionApps() {}
  void settings() {}
  Touch routeTouch(const MappedInputManager& input) {
    if (!input.touchReleased || input.touchAction < 0) return {input.touchPressed, false};
    fui::ActionEvent event;
    event.action = ACTION_CONFIRM;
    event.value = static_cast<int16_t>(input.touchAction);
    confirmationCallback(event, this);
    return {true, true};
  }
  void showInstallConfirmation();
  void showRemoveConfirmation();
  static void confirmationCallback(const fui::ActionEvent&, void*);
  void stepConfirmationFocus(int);
  void confirmationAction(int);
  void confirmInstall();
  void removeConfirmed(bool);
  void buildConfirmation(UiScreen&);
  void activateFocus();
  void onBackButton();
  void guardHeldButtons();
  bool consumeHeldButtons();
  bool inputActive() const;
  void loop();
};

// PRODUCTION_CONFIRMATION
// PRODUCTION_INPUT

struct RecordingTarget : fui::DrawTarget {
  struct Text {
    fui::Rect rect;
    std::string value;
    fui::Color color;
  };
  int16_t lineSize = 24;
  std::vector<Text> texts;
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * lineSize / 2), lineSize};
  }
  int16_t lineHeight(fui::FontId) const override { return lineSize; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect rect, const char* text, fui::TextStyle style) override {
    texts.push_back({rect, text, style.color});
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};
bool same(const fui::Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
bool contains(const Rect& outer, const Rect& inner) {
  return inner.width > 0 && inner.height > 0 && inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width && inner.y + inner.height <= outer.y + outer.height;
}
bool separated(const Rect& a, const Rect& b) {
  return a.x + a.width + 8 <= b.x || b.x + b.width + 8 <= a.x || a.y + a.height + 8 <= b.y || b.y + b.height + 8 <= a.y;
}

void checkActions() {
  for (unsigned permissions : {0u, 5u}) {
    AppStoreActivity store;
    store.entries_[0].package.permissions = permissions;
    store.showInstallConfirmation();
    assert(store.view_ == AppStoreActivity::View::ConfirmInstall && store.primaryFocused_ && store.focus_ == 1);
    assert(store.installations == 0 && store.grants == 0 && store.stops == 0 && store.childStarts == 0);
    assert(store.rowCount_ == (permissions ? 5u : 3u));
    assert(std::strcmp(store.subtitles_[1], "3 KiB (2050 bytes)") == 0);
    if (permissions) {
      assert(std::strcmp(store.labels_[3], "WiFi") == 0);
      assert(std::strcmp(store.labels_[4], "Storage") == 0);
    } else {
      assert(std::strcmp(store.subtitles_[2], "STR_STORE_PERMISSION_NONE") == 0);
    }
    for (size_t i = 0; i < store.rowCount_; ++i) assert(!store.rows_[i].enabled);
    const int removed = crossstore::removals;
    store.confirmationAction(-1);
    store.confirmationAction(2);
    assert(store.installations == 0 && store.detailsCalls == 0);
    store.activateFocus();  // default physical Confirm is Cancel
    assert(store.detailsCalls == 1 && store.installations == 0 && store.grants == 0 && crossstore::removals == removed);
    store.showInstallConfirmation();
    store.onBackButton();
    assert(store.detailsCalls == 2 && store.installations == 0 && store.grants == 0);
    store.showInstallConfirmation();
    store.confirmationAction(0);
    assert(store.installations == 1 && store.grants == 1 && store.stops == 1);
    fui::ActionEvent stale;
    stale.action = AppStoreActivity::ACTION_CONFIRM;
    stale.value = 0;
    AppStoreActivity::confirmationCallback(stale, &store);
    assert(store.installations == 1);  // a prior confirmation event is ignored in Busy
  }
  for (int choice : {0, 1, 2}) {
    AppStoreActivity store;
    const int before = crossstore::removals;
    store.showRemoveConfirmation();
    assert(store.focus_ == 2 && store.primaryFocused_ && store.rowCount_ == 4);
    assert(crossstore::removals == before && store.installations == 0 && store.grants == 0);
    store.confirmationAction(-1);
    store.confirmationAction(3);
    assert(crossstore::removals == before && store.detailsCalls == 0);
    store.confirmationAction(choice);
    assert(crossstore::removals == before + (choice < 2 ? 1 : 0));
    if (choice < 2) {
      assert(crossstore::includeData == (choice == 1) && crossstore::removedId == "test_app");
      assert(store.reloads == 1 && store.stops == 1);
    } else
      assert(store.reloads == 0 && store.stops == 0);
    assert(store.detailsCalls == 1);
  }
  AppStoreActivity back;
  const int before = crossstore::removals;
  back.showRemoveConfirmation();
  back.onBackButton();
  assert(crossstore::removals == before && back.detailsCalls == 1);
  back.showRemoveConfirmation();
  back.activateFocus();
  assert(crossstore::removals == before && back.detailsCalls == 2);
  crossstore::removalSucceeds = false;
  back.showRemoveConfirmation();
  back.confirmationAction(1);
  assert(back.errors == 1 && back.detailsCalls == 2 && back.reloads == 0);
  crossstore::removalSucceeds = true;
  // A network picker can still be cancelled after explicit download consent.
  WiFi.connected = 0;
  WiFi.mode = WIFI_OFF;
  for (bool cancelled : {true, false}) {
    AppStoreActivity store;
    store.showInstallConfirmation();
    assert(store.childStarts == 0 && store.installations == 0);
    store.confirmationAction(0);
    assert(store.childStarts == 1 && store.installations == 0 && store.grants == 0);
    store.childResult(ActivityResult{cancelled});
    assert(store.installations == (cancelled ? 0 : 1));
  }
  WiFi.connected = WL_CONNECTED;
  WiFi.mode = 1;
}

void checkInput() {
  using Button = MappedInputManager::Button;
  for (bool remove : {false, true})
    for (int direction : {-1, 1}) {
      AppStoreActivity store;
      if (remove)
        store.showRemoveConfirmation();
      else
        store.showInstallConfirmation();
      const int actions = remove ? 3 : 2;
      const int slots = static_cast<int>(store.rowCount_) + actions;
      std::vector<bool> visited(slots);
      for (int step = 0; step < slots; ++step) {
        store.stepConfirmationFocus(direction);
        const int index =
            store.primaryFocused_ ? static_cast<int>(store.rowCount_) + store.focus_ : store.nav_.selected.load();
        assert(index >= 0 && index < slots);
        visited[index] = true;
        if (!store.primaryFocused_) {
          const int before = store.detailsCalls;
          store.activateFocus();
          assert(store.detailsCalls == before);
        }
        assert(store.installations == 0 && store.grants == 0);
      }
      assert(std::all_of(visited.begin(), visited.end(), [](bool value) { return value; }));
      assert(store.primaryFocused_ && store.focus_ == actions - 1);
    }
  for (const auto key :
       {Button::Confirm, Button::Back, Button::NavNext, Button::NavPrevious, Button::Up, Button::Down}) {
    AppStoreActivity store;
    const size_t index = static_cast<size_t>(key);
    store.mappedInput.held[index] = true;
    store.showInstallConfirmation();
    assert(store.releasePending_);
    store.loop();
    assert(store.view_ == AppStoreActivity::View::ConfirmInstall && store.installations == 0);
    store.mappedInput = {};
    store.mappedInput.released[index] = true;
    store.loop();
    assert(store.view_ == AppStoreActivity::View::ConfirmInstall && store.installations == 0 && store.focus_ == 1);
    // A fresh touch is independent of an inherited hardware-button barrier.
    store.mappedInput = {};
    store.mappedInput.held[index] = true;
    store.guardHeldButtons();
    store.mappedInput.touchReleased = true;
    store.mappedInput.touchAction = 0;
    store.loop();
    assert(store.installations == 1 && store.grants == 1);
  }
  for (bool remove : {false, true})
    for (const auto key : {Button::Up, Button::Down}) {
      AppStoreActivity store;
      if (remove)
        store.showRemoveConfirmation();
      else
        store.showInstallConfirmation();
      const int initialFocus = store.focus_;
      const size_t index = static_cast<size_t>(key);
      store.mappedInput.held[index] = store.mappedInput.pressed[index] = true;
      store.loop();
      store.mappedInput.pressed[index] = false;
      for (int heldFrame = 0; heldFrame < 20; ++heldFrame) store.loop();
      assert(store.primaryFocused_ && store.focus_ == initialFocus && store.pageTurns == 0);
      store.mappedInput.held[index] = false;
      store.mappedInput.released[index] = true;
      store.loop();
      if (key == Button::Up)
        assert(store.primaryFocused_ && store.focus_ == initialFocus - 1);
      else
        assert(!store.primaryFocused_ && store.nav_.selected == 0);
      assert(store.pageTurns == 0 && store.installations == 0 && store.grants == 0 && store.detailsCalls == 0);
      const int updates = store.updates;
      store.mappedInput = {};
      store.loop();
      assert(store.updates == updates);
    }
}

void checkLayoutAndRender() {
  const std::array<Rect, 4> bounds{Rect{13, 60, 336, 340}, Rect{13, 60, 432, 550}, Rect{13, 60, 912, 330},
                                   Rect{13, 60, 492, 760}};
  for (const auto band : bounds)
    for (int16_t line : {24, 29, 40})
      for (int16_t minimum : {44, 72, 96})
        for (bool remove : {false, true}) {
          AppStoreActivity store;
          store.bounds = band;
          store.renderer.width = band.x + band.width + 13;
          store.renderer.height = band.y + band.height + 30;
          if (remove)
            store.showRemoveConfirmation();
          else
            store.showInstallConfirmation();
          const int actions = remove ? 3 : 2;
          const auto layout = crossstore::StoreConfirmation::fit(band, line, minimum, 20, actions);
          assert(contains(band, layout.list));
          // At least one full information row remains visible with the fixed CTA.
          if (layout.list.height < std::max<int>(44, minimum))
            std::fprintf(stderr, "Insufficient information area: %dx%d font=%d touch=%d actions=%d info=%d\n",
                         band.width, band.height, line, minimum, actions, layout.list.height);
          assert(layout.list.height >= std::max<int>(44, minimum));
          for (int i = 0; i < actions; ++i) {
            assert(contains(band, layout.buttons[i]));
            assert(layout.buttons[i].height >= std::max<int>(44, minimum));
            assert(layout.buttons[i].width >= 44 && separated(layout.list, layout.buttons[i]));
            for (int j = 0; j < i; ++j) assert(separated(layout.buttons[i], layout.buttons[j]));
          }
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
          store.buildConfirmation(screen);
          assert(interactions.count() == static_cast<size_t>(actions));
          for (size_t i = 0; i < interactions.count(); ++i) {
            const auto& hit = interactions.data()[i];
            assert(hit.action == AppStoreActivity::ACTION_CONFIRM && hit.value == static_cast<int>(i));
            assert(same(hit.rect, layout.buttons[i]));
          }
          assert(store.nav_.drawnRows > 0);
          assert(!store.nav_.consumeRebuildNeeded());  // primary focus cannot chase a hidden list selection
          const auto initialTop = store.nav_.top;
          store.scrollDetails(2);
          fui::Frame<24> nextFrame(target, device, input, interactions);
          AppStoreActivity::UiScreen next(nextFrame, theme);
          store.buildConfirmation(next);
          assert(store.nav_.top >= initialTop && interactions.count() == static_cast<size_t>(actions));
          for (size_t i = 0; i < interactions.count(); ++i)
            assert(same(interactions.data()[i].rect, layout.buttons[i]));
        }
}
int main() {
  for (bool mono : {false, true}) {
    BoardConfig::mono = mono;
    checkActions();
    checkInput();
  }
  checkLayoutAndRender();
  std::puts(
      "Production store confirmations: explicit consent, cancellation, keep/all removal, independent input and visible "
      "layout passed");
}
