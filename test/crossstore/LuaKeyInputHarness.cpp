#include <CrossStoreRuntime.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#define CROSSMUX_CHAT 0
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)

static uint32_t now = 0;
uint32_t millis() { return now; }
struct RenderLock {
  static inline bool held = false;
  RenderLock() {
    assert(!held);
    held = true;
  }
  ~RenderLock() {
    assert(held);
    held = false;
  }
};
enum { STR_APPSTORE, STR_STORE_INVALID_PACKAGE, STR_MEMORY_ERROR, STR_STORE_APP_FAILED };
const char* tr(int) { return "test"; }
struct HalMemory {
  struct Heap {
    unsigned totalBytes;
  };
  static Heap getPsramHeap() { return {0}; }
};
namespace memory {
bool psramHasHeadroom(unsigned, unsigned, unsigned) { return true; }
}  // namespace memory
namespace HalBuzzer {
void shutdown() {}
}  // namespace HalBuzzer

static std::string source;
namespace crossstore {
struct Package {
  char id[32] = "key_test", name[96] = "Keys";
  unsigned memoryBytes = 65536;
};
bool validId(const char*) { return true; }
std::string installedPath(const char*) { return "unused"; }
bool readPackage(const std::string&, Package&) { return true; }
bool compatible(const Package&, char*, size_t) { return true; }
unsigned grantedPermissions(const char*) { return 0; }
std::unique_ptr<uint8_t[]> readEntry(const std::string&, const char*, size_t, size_t& size) {
  size = source.size();
  auto result = std::make_unique<uint8_t[]>(size);
  std::memcpy(result.get(), source.data(), size);
  return result;
}
}  // namespace crossstore

struct MappedInputManager {
  enum class Button { Back, Confirm, Left, Right, Up, Down, NavNext, NavPrevious, PageBack, PageForward, Count };
  enum class SwipeDir { None, Left, Right, Up, Down };
  std::array<bool, static_cast<size_t>(Button::Count)> pressed{}, held{}, released{};
  bool tap = false;
  SwipeDir swipe = SwipeDir::None;
  bool wasPressed(Button button) const { return pressed[static_cast<size_t>(button)]; }
  bool isPressed(Button button) const { return held[static_cast<size_t>(button)]; }
  bool wasReleased(Button button) const { return released[static_cast<size_t>(button)]; }
  bool wasScreenTapped(int& x, int& y) const {
    x = y = 10;
    return tap;
  }
  SwipeDir wasSwipe(int* = nullptr, int* = nullptr) const { return swipe; }
  void frame() {
    pressed.fill(false);
    released.fill(false);
    tap = false;
    swipe = SwipeDir::None;
  }
  void press(Button button) {
    frame();
    pressed[static_cast<size_t>(button)] = held[static_cast<size_t>(button)] = true;
  }
  void release(Button button) {
    frame();
    held[static_cast<size_t>(button)] = false;
    released[static_cast<size_t>(button)] = true;
  }
};
struct KeyboardResult {
  std::string text;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<KeyboardResult> data{KeyboardResult{"typed"}};
};
struct WifiSelectionActivity {};
struct KeyboardEntryActivity {};
struct NativeService {
  bool image = false;
  int pumps = 0, networks = 0;
  void pump() { ++pumps; }
  void network(bool) { ++networks; }
  bool imageScreen() const { return image; }
};
struct TouchResult {
  bool routed;
  explicit operator bool() const { return routed; }
};
struct UiListActivity {
  MappedInputManager mappedInput;
  int defaultLoops = 0, defaultMoves = 0, updates = 0, touchRoutes = 0, swipes = 0, finishes = 0, enters = 0;
  struct App {
    bool invalidated() const { return false; }
    template <class T, class U>
    void on(int, T, U) {}
  } app;
  struct Manager {
    void goToLibrary() {}
    void goToFileTransfer() {}
    void goToBrowser() {}
    void goToReader(const char*) {}
  } activityManager;
  static constexpr int ACTION_USER = 3;
  void onEnter() { ++enters; }
  void loop() {
    ++defaultLoops;
    using B = MappedInputManager::Button;
    // This is only the downstream navigation observer. Production Lua loop
    // must not forward a consumed press/hold/release to it at all.
    if (mappedInput.isPressed(B::Up) || mappedInput.isPressed(B::Down)) ++defaultMoves;
  }
  void requestUpdate() { ++updates; }
  void finish() { ++finishes; }
  TouchResult routeTouch(const MappedInputManager& input, bool) {
    if (input.tap) ++touchRoutes;
    return {input.tap};
  }
  bool handleListSwipe(MappedInputManager::SwipeDir swipe, int, int) {
    swipes += swipe != MappedInputManager::SwipeDir::None;
    return swipe != MappedInputManager::SwipeDir::None;
  }
};

class LuaAppActivity : public UiListActivity, public crossstore::Host {
 public:
  LuaAppActivity() : runtime_(*this) {}
  crossstore::Runtime runtime_;
  crossstore::Package package_;
  std::unique_ptr<NativeService> nativeService_;
  bool active_ = false, dirty_ = false, inputPending_ = false, wifiPending_ = false, vmPsram_ = false;
  bool keyReleasePending_ = false, serviceCleanupPending_ = false, exitPending_ = false, keepAwake_ = false;
  bool nativeInputConsumed_ = false, canvasMode_ = false;
  unsigned permissions_ = 0;
  uint32_t tickAt_ = 0;
  int lastWidth_ = 336, lastHeight_ = 430, releasedWifi = 0;
  char id_[32] = "key_test", pendingBook_[512]{}, pendingExport_[32]{}, pendingRoute_[32]{};
  char inputTitle_[96]{}, inputText_[192]{};
  std::vector<std::string> notices;
  std::function<void(const ActivityResult&)> childResult;
  void onEnter();
  void loop();
  void guardHeldButtons();
  bool handleKeyInput();
  void showError(const char*);
  void dispatch(const char*, int = 0, const char* = "", int = -1, int = -1);
  static void canvasAction() {}
  void exportCanvas() { pendingExport_[0] = 0; }
  void releaseSystemWifi() { ++releasedWifi; }
  template <class Child, class... Args>
  void startActivityForResultWith(std::function<void(const ActivityResult&)> callback, Args&&...) {
    assert(!childResult);
    childResult = std::move(callback);
  }
  void returnChild(bool cancelled = false) {
    auto callback = std::move(childResult);
    childResult = nullptr;
    callback(ActivityResult{cancelled});
  }
  void title(const char*) override { dirty_ = true; }
  void clear() override { dirty_ = true; }
  void grid(unsigned) override {}
  void caption(const char*) override { dirty_ = true; }
  bool row(const char*, const char*) override {
    dirty_ = true;
    return true;
  }
  bool capability(const char*) const override { return false; }
  bool chinese() const override { return false; }
  uint32_t milliseconds() const override { return now; }
  bool read(const char*, char*, size_t) override { return false; }
  bool write(const char*, const char*, size_t) override { return true; }
  void input(const char*, const char*) override { inputPending_ = true; }
  void notify(const char* text) override { notices.emplace_back(text); }
  bool open(const char*) override { return false; }
  void size(int& w, int& h) const override {
    w = 336;
    h = 430;
  }
  bool canvas() override {
    canvasMode_ = true;
    return true;
  }
};

// PRODUCTION_METHODS

static constexpr const char* ACCEPT =
    "return {on_key=function(_,d) app.notify(d); app.caption(d); return true end,"
    "on_image=function() app.notify('image') end,on_back=function() app.notify('back') end}";

static void runtimeContract() {
  LuaAppActivity host;
  assert(crossstore::API_VERSION == 8);
  for (const char* returned : {"true", "false", "nil", "0", "1", "'true'", "{}"}) {
    const std::string script = std::string("return {on_key=function(_,d) assert(d=='up'); return ") + returned +
                               " end,on_back=function() return false end}";
    assert(host.runtime_.load(script.data(), script.size()));
    assert(host.runtime_.event("on_key", 0, "up"));
    assert(host.runtime_.handled() == !std::strcmp(returned, "true"));
    assert(host.runtime_.event("on_back") && host.runtime_.handled());
  }
  for (const char* script : {"return {}", "return {on_key=function() end}"}) {
    assert(host.runtime_.load(script, std::strlen(script)));
    assert(host.runtime_.event("on_key", 0, "down") && !host.runtime_.handled());
  }
  for (const char* script : {"return {on_key=1}", "return {on_key=function() error('bad key') end}",
                             "return {on_key=function() while true do end end}"}) {
    assert(host.runtime_.load(script, std::strlen(script)));
    assert(!host.runtime_.event("on_key", 0, "up"));
    assert(!host.runtime_.handled() && host.runtime_.used() == 0);
  }
}

int main() {
  using B = MappedInputManager::Button;
  runtimeContract();
  for (bool canvas : {false, true}) {
    source = ACCEPT;
    LuaAppActivity host;
    host.mappedInput.press(B::Up);  // inherited launch gesture, not a local key
    host.onEnter();
    host.canvasMode_ = canvas;
    host.loop();
    assert(host.notices.empty() && host.defaultLoops == 0);
    host.mappedInput.release(B::Up);
    host.loop();
    assert(host.defaultLoops == 0);
    host.mappedInput.press(B::Up);
    const int updates = host.updates;
    host.loop();
    assert(host.notices.size() == 1 && host.notices.back() == "up");
    assert(host.updates == updates + 1 && !host.dirty_);
    for (unsigned i = 0; i < 20; ++i) {
      host.mappedInput.frame();
      now += 600;
      host.loop();
      assert(host.notices.size() == 1 && host.defaultLoops == 0);
    }
    host.mappedInput.tap = true;
    host.loop();
    assert(host.touchRoutes == 1);  // held buttons never block independent touch
    host.mappedInput.frame();
    host.mappedInput.swipe = MappedInputManager::SwipeDir::Down;
    host.loop();
    assert(host.swipes == 1);
    host.mappedInput.release(B::Up);
    host.loop();
    assert(host.defaultLoops == 0);
    host.mappedInput.press(B::Down);
    host.loop();
    assert(host.notices.size() == 2 && host.notices.back() == "down");
  }

  for (const char* callback :
       {"", "on_key=function(_,d) app.notify(d); return false end", "on_key=function(_,d) app.notify(d) end"}) {
    source = std::string("return {") + callback + "}";
    LuaAppActivity host;
    host.onEnter();
    host.mappedInput.press(B::Up);
    host.loop();
    assert(host.defaultLoops == 1 && host.defaultMoves == 1);
    host.mappedInput.frame();
    now += 1000;
    host.loop();
    assert(host.defaultLoops == 2 && host.defaultMoves == 2);
    host.mappedInput.release(B::Up);
    host.loop();
    assert(host.defaultLoops == 3);
    assert(host.notices.size() == (*callback ? 1u : 0u));
  }

  // Only logical Up/Down opt in. A simultaneous second key stays consumed
  // with the first accepted gesture until both keys are released.
  source = ACCEPT;
  {
    LuaAppActivity host;
    host.onEnter();
    host.mappedInput.press(B::Left);
    host.loop();
    assert(host.notices.empty() && host.defaultLoops == 1);
    host.mappedInput.release(B::Left);
    host.loop();
    host.mappedInput.press(B::Up);
    host.loop();
    host.mappedInput.press(B::Down);
    host.loop();
    host.mappedInput.release(B::Up);
    host.loop();
    host.mappedInput.frame();
    now += 1000;
    host.loop();
    host.mappedInput.release(B::Down);
    host.loop();
    assert(host.notices.size() == 1 && host.defaultLoops == 2);
    host.mappedInput.press(B::Down);
    host.loop();
    assert(host.notices.size() == 2 && host.notices.back() == "down");
  }

  // A throwing key callback changes to the error page but must not navigate it
  // with the failing gesture. The following new gesture can use normal input.
  source = "return {on_key=function() error('key failed') end}";
  {
    LuaAppActivity host;
    host.onEnter();
    host.mappedInput.press(B::Up);
    host.loop();
    assert(!host.active_ && host.defaultLoops == 0 && host.updates > 0);
    host.mappedInput.frame();
    host.loop();
    host.mappedInput.release(B::Up);
    host.loop();
    assert(host.defaultLoops == 0);
    host.mappedInput.frame();
    host.loop();
    assert(host.defaultLoops == 1);
  }

  // Real child-launch lambdas from loop(): test both accepted input and
  // cancelled Wi-Fi result, including a release after parent resumption.
  source =
      "return {on_key=function(_,d) app.notify(d); app.input('Name'); return true end,"
      "on_input=function(_,s) app.notify(s) end}";
  {
    LuaAppActivity host;
    host.onEnter();
    host.mappedInput.press(B::Up);
    host.loop();
    host.mappedInput.frame();
    host.loop();
    assert(host.childResult && host.notices.size() == 1);
    host.mappedInput.release(B::Up);
    host.mappedInput.press(B::Confirm);
    host.returnChild();
    assert(host.notices.back() == "typed");
    host.loop();
    host.mappedInput.tap = true;
    host.loop();
    assert(host.touchRoutes == 1 && host.defaultLoops == 0);
    host.mappedInput.release(B::Confirm);
    host.loop();
    assert(host.defaultLoops == 0);
    host.mappedInput.press(B::Down);
    host.loop();
    assert(host.notices.back() == "down");
  }
  source = ACCEPT;
  {
    LuaAppActivity host;
    host.onEnter();
    host.nativeService_ = std::make_unique<NativeService>();
    host.wifiPending_ = true;
    host.loop();
    host.mappedInput.press(B::Up);
    host.returnChild(true);
    assert(host.releasedWifi == 1 && host.nativeService_->networks == 1);
    host.loop();
    host.mappedInput.release(B::Up);
    host.loop();
    assert(host.notices.empty() && host.defaultLoops == 0);
    host.mappedInput.press(B::Down);
    host.loop();
    assert(host.notices.back() == "down");
  }

  // Native image screens bypass UiListActivity, but not key ownership or the
  // inherited Back release guard. Touch still reaches the native image event.
  {
    LuaAppActivity host;
    host.mappedInput.press(B::Back);
    host.onEnter();
    host.nativeService_ = std::make_unique<NativeService>();
    host.nativeService_->image = true;
    host.mappedInput.release(B::Back);
    host.loop();
    assert(host.notices.empty());
    host.mappedInput.press(B::Up);
    host.loop();
    assert(host.notices.back() == "up");
    host.mappedInput.frame();
    host.mappedInput.tap = true;
    host.loop();
    assert(host.notices.back() == "image" && host.notices.size() == 2);
    host.mappedInput.release(B::Up);
    host.loop();
    assert(host.notices.size() == 2 && host.defaultLoops == 0);
    assert(host.nativeService_->pumps == 4);
  }
  std::puts(
      "Lua API8 keys: production VM/loop, strict opt-in, held/release, child return, touch and native image passed");
}
