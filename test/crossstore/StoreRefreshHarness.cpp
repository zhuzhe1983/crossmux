#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

static uint32_t now = 0;
uint32_t millis() { return now; }
struct RenderLock {
  static inline bool held = false;
  RenderLock() {
    assert(!held);
    held = true;
  }
  ~RenderLock() { held = false; }
};
constexpr int WL_CONNECTED = 1, WIFI_OFF = 0, WIFI_STA = 1;
struct Wifi {
  int connected = WL_CONNECTED, mode = WIFI_STA;
  int status() const { return connected; }
  int getMode() const { return mode; }
  void persistent(bool) {}
  void begin(const char*, const char*) {}
} WiFi;
struct Credential {
  std::string ssid, password;
};
struct Credentials {
  void loadFromFile() {}
  std::string getLastConnectedSsid() const { return {}; }
  std::optional<Credential> getCredentialAt(int) const { return Credential{}; }
  std::optional<Credential> findCredential(const std::string&) const { return Credential{}; }
} WIFI_STORE;
struct Renderer {
  int levels = 16;
  int getGrayscaleLevels() const { return levels; }
  int getLineHeight(int) const { return 20; }
};
namespace NetworkStartup {
bool setMode(Renderer&, int) { return true; }
}  // namespace NetworkStartup
struct Fonts {
  int bodyFontId = 1;
};
Fonts uiScaleSpec(bool) { return {}; }
constexpr const char* STR_STORE_CONNECT_CATALOG = "Connect";
constexpr const char* STR_STORE_CACHED = "Cached";
constexpr const char* STR_STORE_REFRESHING = "Refreshing";
constexpr const char* STR_STORE_ICONS_PENDING = "Icons pending";
constexpr const char* STR_STORE_CURRENT = "Current";
constexpr const char* STR_STORE_CACHE_RETAINED = "Failed; cache retained";
constexpr const char* STR_STORE_GRID_PAGE = "%u/%u (%u)";
const char* tr(const char* text) { return text; }

namespace crossstore {
struct StoreGrid {
  int size = 96;
  int cell(size_t) const { return size; }
  static int iconSize(int size, int, int) { return size; }
};
struct StoreCatalog {
  enum class Status { Empty, Cached, Refreshing, Ready, Failed };
  struct Entry {
    struct {
      char id[32]{};
    } package;
  };
  uint32_t rev = 1, icons = 1;
  Status state = Status::Cached;
  std::array<bool, 16> available{};
  bool cached = true;
  size_t missing = 0;
  int starts = 0, lookups = 0;
  void poll() {}
  uint32_t revision() const { return rev; }
  uint32_t iconRevision() const { return icons; }
  Status status() const { return state; }
  bool refreshing() const { return state == Status::Refreshing; }
  bool hasCache() const { return cached; }
  size_t missingIcons() const { return missing; }
  void startRefresh() {
    ++starts;
    state = WiFi.status() == WL_CONNECTED ? Status::Refreshing : Status::Failed;
  }
  bool iconPath(const Entry& entry, char* path, size_t capacity) {
    assert(RenderLock::held);
    ++lookups;
    std::snprintf(path, capacity, "%s", entry.package.id);
    return available[std::stoi(entry.package.id)];
  }
};
std::string installedPath(const char* id) { return id; }
}  // namespace crossstore

struct Icons {
  std::array<bool, 16> present{}, bundleValid{}, packageValid{};
  int reads = 0, packageReads = 0, clears = 0, lastSize = 0, lastLevels = 0;
  void clear() {
    assert(RenderLock::held);
    present.fill(false);
    ++clears;
  }
  bool loadBundle(size_t slot, const char*, int levels, int size) {
    assert(RenderLock::held);
    ++reads;
    lastSize = size;
    lastLevels = levels;
    return present[slot] = bundleValid[slot];
  }
  bool loadPackage(size_t slot, const char*, int levels, int size) {
    assert(RenderLock::held);
    ++packageReads;
    lastSize = size;
    lastLevels = levels;
    return present[slot] = packageValid[slot];
  }
};

class AppStoreActivity {
 public:
  enum class View { Browser, Details };
  enum class Connection { Pending, Connecting, Idle };
  enum class Tab { All, Installed };
  static constexpr size_t PAGE_SIZE = 16;
  crossstore::StoreCatalog catalog_;
  crossstore::StoreCatalog::Entry entries_[PAGE_SIZE]{};
  std::array<bool, 16> installed{};
  Icons icons_;
  Renderer renderer;
  crossstore::StoreGrid layout;
  size_t count_ = 16, total_ = 22, pageSize_ = 16, offsets_[2]{};
  View view_ = View::Browser;
  Tab tab_ = Tab::All;
  Connection connection_ = Connection::Idle;
  bool wifiStarted_ = false, resumeIcons_ = false, ready = true, held = false;
  uint16_t catalogIconSlots_ = 0, loadedIconSlots_ = 0;
  uint32_t connectionAt_ = 0, revision_ = 1, iconRevision_ = 0, lastIconPaint_ = 0;
  crossstore::StoreCatalog::Status lastStatus_ = crossstore::StoreCatalog::Status::Empty;
  char statusText_[128]{}, pageText_[96]{};
  int redraws = 0, rebuilds = 0;
  AppStoreActivity() {
    for (size_t i = 0; i < PAGE_SIZE; ++i) std::snprintf(entries_[i].package.id, 32, "%zu", i);
    icons_.bundleValid.fill(true);
  }
  crossstore::StoreGrid geometry() const { return layout; }
  int installedIndex(const char* id) const { return installed[std::stoi(id)] ? 0 : -1; }
  bool routingReady() const { return ready; }
  bool inputActive() const { return held; }
  void requestUpdate() {
    assert(RenderLock::held);
    ++redraws;
  }
  void browser(bool) {
    RenderLock lock;
    ++rebuilds;
    revision_ = catalog_.revision();
    loadIcons();
    updateStatus();
    requestUpdate();
  }
  void loadIcons();
  bool refreshIcons();
  bool updateStatus();
  void pumpRefresh();
};

// PRODUCTION_METHODS

int main() {
  using Status = crossstore::StoreCatalog::Status;
  using Connection = AppStoreActivity::Connection;
  for (const int levels : {2, 4, 16}) {
    AppStoreActivity store;
    store.renderer.levels = levels;
    store.layout.size = 32;
    store.catalog_.available.fill(true);
    store.browser(true);
    assert(store.icons_.reads == 16 && store.catalogIconSlots_ == 0xffff);
    assert(store.icons_.lastLevels == levels && store.icons_.lastSize == 32);
    store.redraws = 0;
    const int lookups = store.catalog_.lookups;
    for (int i = 0; i < 22; ++i) {
      now += 2001;
      ++store.catalog_.icons;
      store.pumpRefresh();
    }
    assert(store.redraws == 0 && store.icons_.reads == 16 && store.icons_.clears == 1);
    assert(store.catalog_.lookups == lookups);  // No file access for existing visible bundles.
    store.count_ = 4;
    store.browser(true);
    assert(store.catalogIconSlots_ == 15 && store.loadedIconSlots_ == 15);
    for (size_t i = 4; i < 16; ++i) assert(!store.icons_.present[i]);
  }

  AppStoreActivity fresh;
  fresh.catalog_.state = Status::Refreshing;
  fresh.browser(true);
  fresh.redraws = 0;
  fresh.catalog_.available[15] = true;
  ++fresh.catalog_.icons;
  now += 1999;
  fresh.pumpRefresh();
  assert(fresh.redraws == 0);
  ++now;
  fresh.pumpRefresh();
  assert(fresh.redraws == 1 && fresh.icons_.present[15]);
  assert(fresh.catalogIconSlots_ == 0x8000 && fresh.icons_.reads == 1);
  // A non-visible image arrives; no current-page image changes.
  now += 2001;
  ++fresh.catalog_.icons;
  fresh.pumpRefresh();
  assert(fresh.redraws == 1 && fresh.icons_.reads == 1);
  // Final visible image + terminal state coalesce, even before the 2s window.
  fresh.catalog_.available[0] = true;
  ++fresh.catalog_.icons;
  fresh.catalog_.state = Status::Ready;
  fresh.pumpRefresh();
  assert(fresh.redraws == 2 && fresh.icons_.present[0]);
  assert(!std::strcmp(fresh.statusText_, STR_STORE_CURRENT));

  AppStoreActivity fallback;
  fallback.count_ = 1;
  fallback.installed[0] = fallback.icons_.packageValid[0] = true;
  fallback.browser(true);
  fallback.redraws = 0;
  fallback.catalog_.available[0] = true;
  fallback.icons_.bundleValid[0] = false;
  ++fallback.catalog_.icons;
  fallback.pumpRefresh();
  assert(fallback.redraws == 0 && fallback.icons_.present[0] && fallback.loadedIconSlots_ == 1);
  fallback.icons_.packageValid[0] = false;
  ++fallback.catalog_.icons;
  fallback.pumpRefresh();
  assert(fallback.redraws == 1 && !fallback.icons_.present[0] && fallback.loadedIconSlots_ == 0);
  fallback.icons_.bundleValid[0] = true;
  ++fallback.catalog_.icons;
  fallback.pumpRefresh();
  assert(fallback.redraws == 2 && fallback.icons_.present[0] && fallback.catalogIconSlots_ == 1);

  AppStoreActivity startup;
  startup.connection_ = Connection::Pending;
  startup.browser(true);  // Cached grid, already showing the combined refresh state.
  startup.redraws = 0;
  startup.ready = false;
  startup.pumpRefresh();
  assert(startup.catalog_.starts == 0);  // No network before cached frame publication.
  startup.ready = true;
  WiFi.connected = 0;
  WiFi.mode = WIFI_OFF;
  startup.pumpRefresh();
  assert(startup.connection_ == Connection::Connecting && startup.redraws == 0);
  WiFi.connected = WL_CONNECTED;
  startup.pumpRefresh();
  assert(startup.catalog_.starts == 1 && startup.redraws == 0);
  assert(!std::strcmp(startup.statusText_, STR_STORE_REFRESHING));
  startup.catalog_.state = Status::Failed;
  startup.held = true;
  ++startup.catalog_.rev;
  startup.pumpRefresh();
  assert(startup.redraws == 1 && startup.rebuilds == 1);
  assert(!std::strcmp(startup.statusText_, STR_STORE_CACHE_RETAINED));
  startup.pumpRefresh();
  assert(startup.redraws == 1);  // No idle redraw loop, including during a held gesture.

  AppStoreActivity timeout;
  timeout.connection_ = Connection::Connecting;
  timeout.connectionAt_ = now;
  timeout.browser(true);
  timeout.redraws = 0;
  WiFi.connected = 0;
  now += 12000;
  timeout.pumpRefresh();
  assert(timeout.connection_ == Connection::Idle && timeout.redraws == 1);
  assert(!std::strcmp(timeout.statusText_, STR_STORE_CACHE_RETAINED));
  std::puts(
      "Production store refresh: visible-only icons, no repeated allocation, coalesced startup and immediate failure "
      "passed");
}
