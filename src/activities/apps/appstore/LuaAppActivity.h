#pragma once

#include <CrossStoreRuntime.h>
#include <qrcode.h>

#include <cstdio>

#include "ChatBuildConfig.h"
#if CROSSMUX_CHAT
#include "activities/apps/chat/ChatModel.h"
namespace chat {
class Session;
}
#endif

#include "LuaNativeService.h"
#include "activities/UiListActivity.h"
#include "crossstore/Package.h"

class LuaAppActivity final : public UiListActivity, private crossstore::Host {
 public:
  LuaAppActivity(GfxRenderer& renderer, MappedInputManager& input, const char* id, const char* context = "")
      : UiListActivity("CrossStoreApp", renderer, input, true), runtime_(*this) {
    std::snprintf(id_, sizeof(id_), "%s", id);
    std::snprintf(context_, sizeof(context_), "%s", context);
  }
  bool preventAutoSleep() override { return keepAwake_; }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  friend class LuaPixelService;
  friend class LuaAirPageService;
  friend class LuaWeReadService;
  friend class LuaRadioService;
  bool nativeInputConsumed_ = false;
  bool nativeCanvasFrame_ = false;
  bool serviceCleanupPending_ = false;
  std::unique_ptr<crossstore::NativeService> nativeService_;
  char context_[77]{}, pendingBook_[512]{}, pendingExport_[32]{};
  void exportCanvas();
  uint8_t qrModules_[172]{};
  std::unique_ptr<uint8_t[]> qrExtended_;
  QRCode qrCode_{};
  bool qrReady_ = false, wifiPending_ = false;
#if CROSSMUX_CHAT
  ::chat::Session* chatSession_ = nullptr;
  ::chat::Kind chatKind_ = ::chat::Kind::Radio;
  std::unique_ptr<::chat::Snapshot> chatSnapshot_;
  void releaseChat();
#endif
  bool keepAwake_ = false, systemWifiOwned_ = false;
  bool clockSyncPending_ = false;
  uint32_t clockSyncStarted_ = 0;
  void releaseSystemWifi();
  bool vmPsram_ = false;
  void* resizeAllocation(void*, size_t) override;
  uint32_t permissions_ = 0;
  bool chatWifiOwned_ = false;
  crossstore::Package package_;
  crossstore::Runtime runtime_;
  std::unique_ptr<crossstore::Canvas> canvas_;
  bool canvasMode_ = false, exitPending_ = false;
  char id_[32]{}, heading_[96]{}, inputTitle_[96]{}, inputText_[192]{}, pendingRoute_[32]{};
  // Bounded activity-owned view model; no Lua calls or allocations on the render task.
  char labels_[crossstore::MAX_ROWS][96]{}, values_[crossstore::MAX_ROWS][192]{};
  freeink::ui::ListItem rows_[crossstore::MAX_ROWS]{};
  size_t count_ = 0;
  unsigned columns_ = 0;
  char caption_[192]{};
  bool active_ = false, dirty_ = false, inputPending_ = false;
  bool keyReleasePending_ = false;
  uint32_t tickAt_ = 0;
  int lastWidth_ = 0, lastHeight_ = 0;
  void showError(const char* detail);
  void dispatch(const char* event, int index = 0, const char* text = "", int x = -1, int y = -1);
  void guardHeldButtons();
  bool handleKeyInput();
  int listCount() const override { return static_cast<int>(canvasMode_ && canvas_ ? canvas_->controlCount : count_); }
  const char* headerTitle() const override { return heading_; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleListSwipe(MappedInputManager::SwipeDir swipe, int x, int y) override;
  static void canvasAction(const freeink::ui::ActionEvent& event, void* user);
  Rect canvasBounds() const;
  void buildCanvas(UiScreen& screen);
  void beginDisplayFrame() override;
  void displayFrame() override;
  void title(const char*) override;
  void clear() override;
  void grid(unsigned columns) override {
    columns_ = columns;
    dirty_ = true;
  }
  void caption(const char*) override;
  bool row(const char*, const char*) override;
  bool capability(const char*) const override;
  bool chinese() const override;
  uint32_t milliseconds() const override;
  bool read(const char*, char*, size_t) override;
  bool write(const char*, const char*, size_t) override;
  void input(const char*, const char*) override;
  void notify(const char*) override;
  bool open(const char*) override;
  void size(int& width, int& height) const override;
  bool canvas() override;
  bool draw(const crossstore::DrawCommand&, const char*) override;
  void exit() override { exitPending_ = true; }
  const char* context() const override { return context_; }
  bool system(const char*, const char*, int64_t, crossstore::ServiceReply&) override;
  bool chat(const char*, const char*, int64_t, const char*, crossstore::ServiceReply&) override;
  void measure(const char*, int, unsigned, int&, int&) override;
  bool qr(int, int, int, const char*) override;
  bool sound(const char*) override;
  bool asset(const char*, size_t, char*, size_t, size_t&) override;
  bool allowed(const char*) const override;
};
