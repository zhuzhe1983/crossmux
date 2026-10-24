#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

class MappedInputManager {
 public:
  enum class Button { Confirm, Back, NavNext, NavPrevious, Up, Down };
  enum class SwipeDir { None, Left, Right, Up, Down };
  std::array<bool, 6> held{}, pressed{}, released{};
  bool touchPressed = false, touchHeld = false, touchReleased = false, tapped = false;
  SwipeDir swipe = SwipeDir::None;
  bool isPressed(Button b) const { return held[static_cast<size_t>(b)]; }
  bool wasPressed(Button b) const { return pressed[static_cast<size_t>(b)]; }
  bool wasReleased(Button b) const { return released[static_cast<size_t>(b)]; }
  bool wasScreenTouchPressed(int&, int&) const { return touchPressed; }
  bool isScreenTouchHeld(int&, int&) const { return touchHeld; }
  bool wasScreenTouchReleased() const { return touchReleased; }
  SwipeDir wasSwipe() const { return swipe; }
};

namespace BoardConfig {
bool mono = false;
bool isPaperMono() { return mono; }
}  // namespace BoardConfig

struct RenderLock {
  static inline bool held = false;
  RenderLock() {
    assert(!held);
    held = true;
  }
  ~RenderLock() { held = false; }
};
constexpr int WL_CONNECTED = 1, WIFI_OFF = 0, WIFI_STA = 1;
struct FakeWifi {
  int status() const { return WL_CONNECTED; }
  int getMode() const { return WIFI_STA; }
  void persistent(bool) {}
  void begin(const char*, const char*) {}
} WiFi;
struct Credential {
  std::string ssid, password;
};
struct Credentials {
  void loadFromFile() {}
  std::string getLastConnectedSsid() const { return {}; }
  std::optional<Credential> getCredentialAt(int) const { return std::nullopt; }
  std::optional<Credential> findCredential(const std::string&) const { return std::nullopt; }
} WIFI_STORE;
namespace NetworkStartup {
bool setMode(int&, int) { return false; }
}  // namespace NetworkStartup
uint32_t millis() { return 1; }

struct Catalog {
  enum class Status { Cached, Refreshing, Ready };
  uint32_t rev = 1, icons = 1;
  Status state = Status::Ready;
  void poll() {}
  uint32_t revision() const { return rev; }
  uint32_t iconRevision() const { return icons; }
  Status status() const { return state; }
  bool refreshing() const { return state == Status::Refreshing; }
  size_t missing = 0;
  int starts = 0;
  size_t missingIcons() const { return missing; }
  void startRefresh() {
    ++starts;
    state = Status::Refreshing;
  }
};

class AppStoreActivity {
 public:
  enum class View { Browser, Details, ConfirmInstall, ConfirmRemove };
  enum class Connection { Pending, Connecting, Idle };
  MappedInputManager mappedInput;
  struct Buttons {
    const MappedInputManager& input;
    template <typename F>
    void onNextRelease(F fn) {
      if (input.wasReleased(MappedInputManager::Button::NavNext)) fn();
    }
    template <typename F>
    void onPreviousRelease(F fn) {
      if (input.wasReleased(MappedInputManager::Button::NavPrevious)) fn();
    }
  } buttons_{mappedInput};
  struct Navigation {
    int delta = 0;
    int inputPageRows() const { return 4; }
    void requestScroll(int value) { delta += value; }
  } nav_;
  struct App {
    bool invalidated() const { return false; }
  } app;
  struct Geometry {
    int capacity = 16;
  } shape;
  struct Touch {
    bool routed, dispatched;
    explicit operator bool() const { return dispatched; }
  };
  Catalog catalog_;
  Catalog::Status lastStatus_ = Catalog::Status::Ready;
  View view_ = View::Browser;
  Connection connection_ = Connection::Idle;
  bool releasePending_ = false, wifiStarted_ = false, resumeIcons_ = false;
  uint32_t revision_ = 1, iconRevision_ = 1, connectionAt_ = 0, lastIconPaint_ = 0;
  size_t pageSize_ = 16;
  int renderer = 0, confirms = 0, backs = 0, steps = 0, touches = 0, pages = 0, redraws = 0, rebuilds = 0;
  bool routingReady() const { return true; }
  Geometry geometry() const { return shape; }
  void browser(bool) {
    ++rebuilds;
    revision_ = catalog_.revision();
    pageSize_ = shape.capacity;
  }
  void requestUpdate() { ++redraws; }
  bool updateStatus() {
    const bool changed = lastStatus_ != catalog_.status();
    lastStatus_ = catalog_.status();
    return changed;
  }
  bool refreshIcons() {
    iconRevision_ = catalog_.iconRevision();
    return false;
  }
  void activateFocus() { ++confirms; }
  void onBackButton() { ++backs; }
  void stepFocus(int direction) { steps += direction; }
  void turnPage(int direction) { pages += direction; }
  void scrollDetails(int rows) { nav_.requestScroll(rows); }
  Touch routeTouch(const MappedInputManager& input) {
    if (input.tapped) ++touches;
    return {input.touchPressed || input.touchReleased, input.tapped};
  }
  void guardHeldButtons();
  bool consumeHeldButtons();
  bool inputActive() const;
  void loop();
  void pumpRefresh();
};

// PRODUCTION_METHODS

int main() {
  AppStoreActivity resume;
  resume.resumeIcons_ = true;
  resume.catalog_.missing = 2;
  resume.view_ = AppStoreActivity::View::Details;
  resume.pumpRefresh();
  assert(resume.catalog_.starts == 0);
  resume.view_ = AppStoreActivity::View::Browser;
  resume.pumpRefresh();
  assert(resume.catalog_.starts == 1 && !resume.resumeIcons_);
  // A failed bounded retry does not become an automatic hot refresh loop.
  resume.catalog_.state = Catalog::Status::Ready;
  resume.pumpRefresh();
  assert(resume.catalog_.starts == 1);
  using Button = MappedInputManager::Button;
  for (const auto key : {Button::Confirm, Button::Back, Button::NavNext, Button::NavPrevious}) {
    AppStoreActivity store;
    const auto index = static_cast<size_t>(key);
    store.mappedInput.held[index] = true;
    store.guardHeldButtons();
    assert(store.releasePending_);
    // A child returned while this key was still held; independent touch works.
    store.mappedInput.touchReleased = store.mappedInput.tapped = true;
    store.loop();
    assert(store.touches == 1 && store.confirms == 0 && store.backs == 0 && store.steps == 0);
    store.mappedInput = {};
    store.mappedInput.released[index] = true;
    store.loop();
    assert(!store.releasePending_);
    assert(store.confirms == 0 && store.backs == 0 && store.steps == 0);
    // A complete new gesture is accepted exactly once after the old release.
    store.mappedInput = {};
    store.mappedInput.pressed[index] = store.mappedInput.held[index] = true;
    store.loop();
    store.mappedInput = {};
    store.mappedInput.released[index] = true;
    store.loop();
    assert(store.confirms == (key == Button::Confirm ? 1 : 0));
    assert(store.backs == (key == Button::Back ? 1 : 0));
    assert(store.steps == (key == Button::NavNext ? 1 : key == Button::NavPrevious ? -1 : 0));
  }

  for (const auto key : {Button::Confirm, Button::Back, Button::NavNext, Button::NavPrevious}) {
    AppStoreActivity store;
    const auto index = static_cast<size_t>(key);
    store.catalog_.rev = 2;
    store.mappedInput.pressed[index] = store.mappedInput.held[index] = true;
    store.loop();
    assert(store.rebuilds == 0 && store.revision_ == 1);
    store.mappedInput.pressed[index] = false;
    store.loop();
    assert(store.rebuilds == 0);
    store.mappedInput = {};
    store.mappedInput.released[index] = true;
    store.loop();
    assert(store.rebuilds == 0);  // release still belongs to the old grid
    store.mappedInput = {};
    store.loop();
    assert(store.rebuilds == 1 && store.revision_ == 2);
  }

  AppStoreActivity touch;
  touch.catalog_.rev = 2;
  touch.mappedInput.touchPressed = touch.mappedInput.touchHeld = true;
  touch.loop();
  touch.mappedInput.touchPressed = false;
  touch.loop();
  assert(touch.rebuilds == 0);
  touch.mappedInput = {};
  touch.mappedInput.touchReleased = touch.mappedInput.tapped = true;
  touch.loop();
  assert(touch.touches == 1 && touch.rebuilds == 0);
  touch.mappedInput = {};
  touch.loop();
  assert(touch.rebuilds == 1);

  // A simultaneous orientation change obeys the same complete-gesture rule.
  AppStoreActivity rotate;
  rotate.shape.capacity = 8;
  rotate.mappedInput.touchHeld = true;
  rotate.loop();
  assert(rotate.rebuilds == 0 && rotate.pageSize_ == 16);
  rotate.mappedInput = {};
  rotate.mappedInput.touchReleased = true;
  rotate.loop();
  assert(rotate.rebuilds == 0);
  rotate.mappedInput = {};
  rotate.loop();
  assert(rotate.rebuilds == 1 && rotate.pageSize_ == 8);

  BoardConfig::mono = true;
  for (const auto key : {Button::Up, Button::Down}) {
    const auto index = static_cast<size_t>(key);
    const auto alias = static_cast<size_t>(key == Button::Up ? Button::NavPrevious : Button::NavNext);
    const int direction = key == Button::Up ? -1 : 1;
    AppStoreActivity store;
    store.mappedInput.pressed[index] = store.mappedInput.held[index] = true;
    store.mappedInput.pressed[alias] = store.mappedInput.held[alias] = true;
    store.loop();
    assert(store.pages == direction && store.steps == 0);
    store.mappedInput.pressed.fill(false);
    for (int frame = 0; frame < 20; ++frame) store.loop();
    assert(store.pages == direction && store.steps == 0);
    // A touch may enter a detail view while the page key is still held.
    store.mappedInput.tapped = store.mappedInput.touchReleased = true;
    store.loop();
    assert(store.touches == 1);
    store.view_ = AppStoreActivity::View::Details;
    store.mappedInput = {};
    store.mappedInput.released[index] = store.mappedInput.released[alias] = true;
    store.loop();
    assert(store.pages == direction && store.steps == 0);
    // The next complete key gesture is ordinary detail navigation.
    store.mappedInput = {};
    store.mappedInput.pressed[index] = store.mappedInput.held[index] = true;
    store.mappedInput.pressed[alias] = store.mappedInput.held[alias] = true;
    store.loop();
    store.mappedInput = {};
    store.mappedInput.released[index] = store.mappedInput.released[alias] = true;
    store.loop();
    assert(store.steps == direction && store.pages == direction);
  }
  AppStoreActivity both;
  for (const auto key : {Button::Up, Button::Down, Button::NavNext, Button::NavPrevious})
    both.mappedInput.pressed[static_cast<size_t>(key)] = both.mappedInput.held[static_cast<size_t>(key)] = true;
  both.loop();
  assert(both.pages == 0 && both.steps == 0);
  AppStoreActivity keyboard;
  keyboard.mappedInput.released[static_cast<size_t>(Button::NavNext)] = true;
  keyboard.loop();
  assert(keyboard.pages == 0 && keyboard.steps == 1);
  BoardConfig::mono = false;
  AppStoreActivity other;
  other.mappedInput.pressed[static_cast<size_t>(Button::Down)] = true;
  other.mappedInput.pressed[static_cast<size_t>(Button::NavNext)] = true;
  other.loop();
  other.mappedInput = {};
  other.mappedInput.released[static_cast<size_t>(Button::Down)] = true;
  other.mappedInput.released[static_cast<size_t>(Button::NavNext)] = true;
  other.loop();
  assert(other.pages == 0 && other.steps == 1);

  std::puts("Production store input: inherited releases, independent touch and deferred catalog/layout refresh passed");
}
