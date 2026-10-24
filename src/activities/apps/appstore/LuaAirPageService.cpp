#include <HalClock.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>

#include "LuaAppActivity.h"
#include "activities/apps/airpage/AirPageConnection.h"
#include "activities/apps/airpage/AirPageDeviceId.h"
#include "activities/apps/airpage/AirPageImageRenderer.h"
#include "activities/apps/airpage/AirPageImageStore.h"
#include "activities/apps/airpage/AirPageWallpaper.h"
#include "network/CrossMuxEndpoints.h"
#include "network/HttpDownloader.h"

class LuaAirPageService final : public crossstore::NativeService {
 public:
  explicit LuaAirPageService(LuaAppActivity& owner) : owner_(owner), connection_(owner.renderer, true) {}
  ~LuaAirPageService() override {
    connection_.stop();
    airpage::AirPageImageRenderer::releaseSessionResources();
  }
  bool command(const char* name, const char* argument, int64_t index, crossstore::ServiceReply& reply) override {
    if (!std::strcmp(name, "airpage.start")) {
      initialize_ = true;
      return true;
    }
    if (!ready_) return false;
    if (!std::strcmp(name, "airpage.status")) {
      std::strcpy(reply.text, status_);
      reply.a = images_.historyCount();
      reply.b = revision_;
      reply.c = imageScreen();
      return true;
    }
    if (!std::strcmp(name, "airpage.url")) {
      std::snprintf(reply.text, sizeof(reply.text), "https://%s%s/?id=%s&w=%u&h=%u&mode=gray%u",
                    CrossMuxEndpoints::AIRPAGE_SUBDOMAIN, CrossMuxEndpoints::host(), airpage::deviceId().c_str(),
                    static_cast<unsigned>(owner_.renderer.getDisplayHeight()),
                    static_cast<unsigned>(owner_.renderer.getDisplayWidth()), owner_.renderer.getGrayscaleLevels());
      return true;
    }
    if (!std::strcmp(name, "airpage.fetch")) {
      fetch_ = true;
      connect();
      return true;
    }
    if (!std::strcmp(name, "airpage.live")) {
      const bool enable = !std::strcmp(argument, "on");
      if (enable && !owner_.allowed("keep_awake")) return false;
      live_ = enable;
      reconnect_ = true;
      owner_.keepAwake_ = live_;
      return true;
    }
    if (!std::strcmp(name, "airpage.hide")) {
      imageScreen_.store(false);
      owner_.requestUpdate();
      return true;
    }
    if (!std::strcmp(name, "airpage.show")) {
      if (index < 0 || index > static_cast<int64_t>(images_.historyCount())) return false;
      const bool found = index == 0 ? images_.selectCurrent(selected_) : images_.selectHistory(index - 1, selected_);
      if (!found) return false;
      imageScreen_.store(true);
      paint_.store(Paint::Pending);
      owner_.requestUpdate();
      return true;
    }
    if (!std::strcmp(name, "airpage.history")) {
      if (index < 1 || static_cast<size_t>(index) > images_.historyCount()) return false;
      const auto& item = images_.historyEntry(index - 1);
      std::snprintf(reply.text, sizeof(reply.text), "%llu", static_cast<unsigned long long>(item.archiveId));
      reply.a = item.image.width;
      reply.b = item.image.height;
      reply.c = item.isCurrent();
      return true;
    }
    if (!std::strcmp(name, "airpage.page")) {
      if (!std::strcmp(argument, "next")) {
        if (!images_.hasNextHistoryPage()) return false;
        page_ = Page::Next;
      } else if (!std::strcmp(argument, "previous")) {
        if (!images_.hasPreviousHistoryPage()) return false;
        page_ = Page::Previous;
      } else if (!std::strcmp(argument, "first"))
        page_ = Page::First;
      else
        return false;
      return true;
    }
    if (!std::strcmp(name, "airpage.wallpaper")) {
      if (!owner_.allowed("wallpaper")) return false;
      wallpaper_ = true;
      return true;
    }
    return false;
  }
  bool imageScreen() const override { return imageScreen_.load(); }
  void renderImage(GfxRenderer& renderer) override {
    const auto result = airpage::AirPageImageRenderer::render(
        renderer, Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()}, selected_);
    paint_.store(result == airpage::AirPageImageRenderer::Result::Success ? Paint::Succeeded : Paint::Failed);
  }
  void network(bool connected) override {
    connection_.acceptWifiSelection(connected);
    if (!connected) {
      fetch_ = false;
      setStatus("offline");
    }
  }
  void pump() override {
    if (initialize_) {
      initialize_ = false;
      {
        RenderLock lock;
        images_.initialize(dateKey());
        ready_ = true;
        setStatus(images_.hasImage() ? "ready" : "empty");
      }
      connection_.begin(false);
    }
    if (!ready_) return;
    if (page_ != Page::None) {
      RenderLock lock;
      switch (page_) {
        case Page::None:
          break;
        case Page::First:
          images_.firstHistoryPage();
          break;
        case Page::Next:
          images_.nextHistoryPage();
          break;
        case Page::Previous:
          images_.previousHistoryPage();
          break;
      }
      page_ = Page::None;
      ++revision_;
    }
    if (reconnect_) {
      reconnect_ = false;
      connection_.setRealtime(live_);
      if (live_) connect();
    }
    switch (paint_.exchange(Paint::Idle)) {
      case Paint::Idle:
      case Paint::Pending:
        break;
      case Paint::Succeeded: {
        RenderLock lock;
        if (selected_.current && images_.hasPendingDownload()) images_.commitDisplayedDownload(dateKey());
        setStatus("ready");
        break;
      }
      case Paint::Failed: {
        RenderLock lock;
        images_.rejectDisplayedImage(selected_);
        imageScreen_.store(false);
        setStatus("invalid");
        owner_.requestUpdate();
        break;
      }
    }
    switch (connection_.pump(!fetch_)) {
      case airpage::AirPageConnection::Event::PushRequested:
        fetch_ = true;
        break;
      case airpage::AirPageConnection::Event::WifiRequired:
      case airpage::AirPageConnection::Event::WifiFailed:
      case airpage::AirPageConnection::Event::RealtimePaused:
        setStatus("offline");
        break;
      case airpage::AirPageConnection::Event::None:
      case airpage::AirPageConnection::Event::StateChanged:
      case airpage::AirPageConnection::Event::RealtimeRetrying:
        break;
    }
    if (wallpaper_) {
      wallpaper_ = false;
      RenderLock lock;
      const bool found = selected_.image.format != airpage::ImageFormat::None || images_.selectCurrent(selected_);
      const bool saved = owner_.allowed("wallpaper") && found && airpage::AirPageWallpaper::install(selected_);
      setStatus(saved ? "wallpaper" : "failed");
    }
    if (!fetch_ || WiFi.status() != WL_CONNECTED) return;
    fetch_ = false;
    setStatus("downloading");
    owner_.dispatch("on_service");
    if (!owner_.active_) return;
    owner_.requestUpdateAndWait();
    RenderLock lock;
    if (!images_.ensureDirectories()) {
      setStatus("failed");
      return;
    }
    char url[192];
    std::snprintf(url, sizeof(url), "https://%s%s/api/device/%s/latest", CrossMuxEndpoints::AIRPAGE_SUBDOMAIN,
                  CrossMuxEndpoints::host(), airpage::deviceId().c_str());
    auto error = HttpDownloader::downloadToFile(url, airpage::AirPageImageStore::kDownloadPartPath);
    if (error != HttpDownloader::OK) {
      const size_t size = std::strlen(url);
      std::snprintf(url + size, sizeof(url) - size, ".bmp");
      error = HttpDownloader::downloadToFile(url, airpage::AirPageImageStore::kDownloadPartPath);
    }
    if (error != HttpDownloader::OK) {
      setStatus("failed");
      return;
    }
    switch (images_.stageDownloadedImage(dateKey())) {
      case airpage::AirPageImageStore::StageResult::Failed:
        setStatus("invalid");
        return;
      case airpage::AirPageImageStore::StageResult::Unchanged:
      case airpage::AirPageImageStore::StageResult::PendingDisplay:
        break;
    }
    if (!images_.selectCurrent(selected_)) {
      setStatus("invalid");
      return;
    }
    airpage::AirPageImageRenderer::resetSessionFailures();
    imageScreen_.store(true);
    paint_.store(Paint::Pending);
    owner_.requestUpdate();
  }

 private:
  enum class Paint : uint8_t { Idle, Pending, Succeeded, Failed };
  enum class Page : uint8_t { None, First, Next, Previous };
  Page page_ = Page::None;
  LuaAppActivity& owner_;
  airpage::AirPageConnection connection_;
  airpage::AirPageImageStore images_;
  airpage::SelectedImage selected_;
  std::atomic<bool> imageScreen_{false};
  std::atomic<Paint> paint_{Paint::Idle};
  bool initialize_ = false, ready_ = false, fetch_ = false, live_ = false, reconnect_ = false, wallpaper_ = false;
  uint32_t revision_ = 0;
  const char* status_ = "empty";
  void setStatus(const char* status) {
    status_ = status;
    ++revision_;
  }
  void connect() {
    if (WiFi.status() == WL_CONNECTED) return;
    owner_.systemWifiOwned_ = owner_.systemWifiOwned_ || WiFi.getMode() == WIFI_OFF;
    owner_.wifiPending_ = true;
  }
  static uint64_t dateKey() {
    std::tm local{};
    if (!halClock.hasValidTime() || !halClock.localTime(local)) return 0;
    return static_cast<uint64_t>(local.tm_year + 1900) * 10000000000ULL +
           static_cast<uint64_t>(local.tm_mon + 1) * 100000000ULL + local.tm_mday * 1000000ULL +
           local.tm_hour * 10000ULL + local.tm_min * 100ULL + local.tm_sec;
  }
};

std::unique_ptr<crossstore::NativeService> crossstore::makeAirPageService(LuaAppActivity& owner) {
  // One bounded image/history/connection context, retained across callbacks;
  // image decoding uses the existing grayscale buffers, not a second framebuffer.
  auto service = makeUniqueNoThrow<LuaAirPageService>(std::ref(owner));
  if (!service)
    LOG_ERR("STORE", "OOM allocating AirPage service (%u bytes)", static_cast<unsigned>(sizeof(LuaAirPageService)));
  return service;
}
