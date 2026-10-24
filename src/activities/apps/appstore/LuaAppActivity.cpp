#include "LuaAppActivity.h"

#include <HalBuzzer.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#if CROSSMUX_CHAT
#include "activities/apps/chat/ChatService.h"
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "crossstore/Permissions.h"
#include "crossstore/RecoverableFile.h"
#include "system/SystemMessages.h"

void LuaAppActivity::onEnter() {
  UiListActivity::onEnter();
  guardHeldButtons();
  app.on(ACTION_USER, &LuaAppActivity::canvasAction, this);
  RenderLock lock;
  title(tr(STR_APPSTORE));
  if (!crossstore::validId(id_)) {
    showError(tr(STR_STORE_INVALID_PACKAGE));
    return;
  }
  const auto path = crossstore::installedPath(id_);
  if (!crossstore::readPackage(path, package_) || std::strcmp(id_, package_.id)) {
    showError(tr(STR_STORE_INVALID_PACKAGE));
    return;
  }
  title(package_.name);
  permissions_ = crossstore::grantedPermissions(id_);
  char missing[160];
  if (!crossstore::compatible(package_, missing, sizeof(missing))) {
    showError(missing);
    return;
  }
  size_t scriptBytes = 0;
  auto source = crossstore::readEntry(path, "main.lua", crossstore::MAX_SCRIPT_BYTES, scriptBytes);
  if (!source) {
    showError(tr(STR_STORE_INVALID_PACKAGE));
    return;
  }
  vmPsram_ = HalMemory::getPsramHeap().totalBytes > 0;
  if (vmPsram_ && !memory::psramHasHeadroom(package_.memoryBytes, package_.memoryBytes, 0)) {
    showError(tr(STR_MEMORY_ERROR));
    return;
  }
  active_ = runtime_.load(reinterpret_cast<const char*>(source.get()), scriptBytes, package_.memoryBytes);
  if (!active_) showError(runtime_.error());
  LOG_INF("STORE", "App %s: VM peak %u / %u bytes", id_, static_cast<unsigned>(runtime_.peak()),
          static_cast<unsigned>(package_.memoryBytes));
}

void LuaAppActivity::onExit() {
  // ActivityManager already owns RenderLock here. Give a live app its final
  // bounded save callback before teardown, including Home and deep sleep.
  if (active_ && !runtime_.event("on_stop")) LOG_ERR("STORE", "App %s final save failed: %s", id_, runtime_.error());
  nativeService_.reset();
  HalBuzzer::shutdown();
#if CROSSMUX_CHAT
  releaseChat();
#endif
  releaseSystemWifi();
  keepAwake_ = false;
  runtime_.close();
  canvas_.reset();
  qrExtended_.reset();
  active_ = false;
  Activity::onExit();
}

void LuaAppActivity::releaseSystemWifi() {
  if (systemWifiOwned_) {
#if CROSSMUX_CHAT
    if (::chat::service::background(::chat::Kind::Wifi) || ::chat::service::background(::chat::Kind::EspNow)) {
      ::chat::service::ownWifi();
    } else
#endif
    {
      WiFi.disconnect(false);
      WiFi.mode(WIFI_OFF);
    }
    systemWifiOwned_ = false;
  }
}

void LuaAppActivity::showError(const char* detail) {
  LOG_ERR("STORE", "App %s stopped: %s", id_, detail);
  active_ = false;
  // A service can be pumping while it dispatches a Lua event. Defer destroying
  // that service until the next loop, after its method has returned.
  serviceCleanupPending_ = true;
  keepAwake_ = wifiPending_ = inputPending_ = false;
  pendingRoute_[0] = pendingExport_[0] = 0;
  clear();
  row(tr(STR_STORE_APP_FAILED), detail);
}

void LuaAppActivity::dispatch(const char* event, int index, const char* text, int x, int y) {
  if (!active_) return;
  RenderLock lock;
  if (!runtime_.event(event, index, text, x, y)) showError(runtime_.error());
}

void LuaAppActivity::guardHeldButtons() {
  using Button = MappedInputManager::Button;
  keyReleasePending_ = false;
  for (const auto button : {Button::Back, Button::Confirm, Button::Left, Button::Right, Button::Up, Button::Down,
                            Button::NavNext, Button::NavPrevious, Button::PageBack, Button::PageForward}) {
    keyReleasePending_ |= mappedInput.isPressed(button);
  }
}

bool LuaAppActivity::handleKeyInput() {
  if (keyReleasePending_) {
    guardHeldButtons();
    // Consume the clearing release frame too. Touch remains independently
    // routable below; no input update or synthetic release is needed.
    return true;
  }
  if (!active_) return false;
  using Button = MappedInputManager::Button;
  constexpr std::array keys{Button::Up, Button::Down};
  return std::any_of(keys.begin(), keys.end(), [this](const auto button) {
    if (!mappedInput.wasPressed(button)) return false;
    dispatch("on_key", 0, button == Button::Up ? "up" : "down");
    if (!active_ || runtime_.handled()) {
      // An accepted press owns its whole gesture, including the base list's
      // long-press repeat. A failed callback must not operate its error page.
      guardHeldButtons();
      return true;
    }
    return false;
  });
}

void LuaAppActivity::loop() {
  if (serviceCleanupPending_) {
    serviceCleanupPending_ = false;
    nativeService_.reset();
#if CROSSMUX_CHAT
    releaseChat();
#endif
    releaseSystemWifi();
    HalBuzzer::shutdown();
  }
  if (exitPending_) {
    finish();
    return;
  }
  if (pendingExport_[0]) {
    exportCanvas();
    return;
  }
  if (wifiPending_) {
    wifiPending_ = false;
    startActivityForResultWith<WifiSelectionActivity>([this](const ActivityResult& result) {
      guardHeldButtons();
      if (nativeService_) nativeService_->network(!result.isCancelled);
      if (result.isCancelled) releaseSystemWifi();
      dispatch("on_network", result.isCancelled ? 0 : 1);
    });
    return;
  }
  if (inputPending_) {
    inputPending_ = false;
    startActivityForResultWith<KeyboardEntryActivity>(
        [this](const ActivityResult& result) {
          guardHeldButtons();
          if (!result.isCancelled) dispatch("on_input", 0, std::get<KeyboardResult>(result.data).text.c_str());
        },
        inputTitle_, inputText_, 191);
    return;
  }
  if (pendingRoute_[0]) {
    char route[32];
    std::strcpy(route, pendingRoute_);
    pendingRoute_[0] = 0;
    if (!std::strcmp(route, "library"))
      activityManager.goToLibrary();
    else if (!std::strcmp(route, "file_transfer"))
      activityManager.goToFileTransfer();
    else if (!std::strcmp(route, "opds"))
      activityManager.goToBrowser();
    else if (!std::strcmp(route, "reading_book"))
      activityManager.goToReader(pendingBook_);
    return;
  }
  if (active_ && nativeService_) nativeService_->pump();
  if (nativeInputConsumed_) {
    nativeInputConsumed_ = false;
    guardHeldButtons();
    return;
  }
  int width, height;
  size(width, height);
  if (width != lastWidth_ || height != lastHeight_) {
    lastWidth_ = width;
    lastHeight_ = height;
    dispatch("on_resize");
  }
  if (millis() - tickAt_ >= 500) {
    tickAt_ = millis();
    dispatch("on_tick");
  }
  const bool buttonsConsumed = handleKeyInput();
  if (dirty_) {
    dirty_ = false;
    requestUpdate();
  }
  if (active_ && nativeService_ && nativeService_->imageScreen()) {
    int x = 0, y = 0;
    if (!buttonsConsumed && mappedInput.wasReleased(MappedInputManager::Button::Back))
      dispatch("on_back");
    else if ((!buttonsConsumed && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) ||
             mappedInput.wasScreenTapped(x, y))
      dispatch("on_image", 0, "", x, y);
    else {
      const auto swipe = mappedInput.wasSwipe();
      if (swipe != MappedInputManager::SwipeDir::None) handleListSwipe(swipe, -1, -1);
    }
    return;
  }
  if (buttonsConsumed) {
    const auto touch = routeTouch(mappedInput, true);
    if (touch.routed && app.invalidated()) requestUpdate();
    if (!touch) {
      int x = 0, y = 0;
      const auto swipe = mappedInput.wasSwipe(&x, &y);
      if (swipe != MappedInputManager::SwipeDir::None) handleListSwipe(swipe, x, y);
    }
    return;
  }
  UiListActivity::loop();
}

void LuaAppActivity::render(RenderLock&& lock) {
  if (active_ && nativeService_ && nativeService_->imageScreen())
    nativeService_->renderImage(renderer);
  else
    UiListActivity::render(std::move(lock));
}

void LuaAppActivity::beginDisplayFrame() {
  nativeCanvasFrame_ = active_ && canvasMode_ && nativeService_ && nativeService_->beginCanvasFrame(renderer);
}

void LuaAppActivity::displayFrame() {
  if (nativeCanvasFrame_ && nativeService_)
    nativeService_->displayCanvasFrame(renderer);
  else
    renderer.displayBuffer();
  nativeCanvasFrame_ = false;
}

void LuaAppActivity::buildScreen(UiScreen& screen) {
  if (canvasMode_ && canvas_) {
    buildCanvas(screen);
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (columns_) {
    const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, true);
    const int x = safe.x + metrics.contentSidePadding;
    const int width = std::max(1, safe.width - 2 * metrics.contentSidePadding);
    int y = safe.y + metrics.topPadding + metrics.headerHeight + 8;
    const int bottom = safe.y + safe.height - metrics.buttonHintsHeight - 8;
    if (*caption_) {
      auto style = screen.theme().bodyText;
      style.align = freeink::ui::TextAlign::Center;
      screen.target().text({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                            static_cast<int16_t>(metrics.headerHeight)},
                           caption_, style);
      y += metrics.headerHeight + 12;
    }
    constexpr int gap = 6;
    const unsigned columns = std::min(columns_, static_cast<unsigned>(std::max(1, (width + gap) / 46)));
    const int visibleRows = std::max(1, (bottom - y + gap) / 46);
    const int capacity = std::min(20, visibleRows * static_cast<int>(columns));
    syncPagedViewport(capacity);
    freeink::ui::TileGridItem tiles[20]{};
    size_t visible = 0;
    for (size_t i = static_cast<size_t>(nav.top); i < count_ && visible < static_cast<size_t>(capacity); ++i) {
      tiles[visible].label = labels_[i];
      tiles[visible].value = static_cast<int16_t>(i);
      tiles[visible].state = static_cast<int>(i) == nav.selected ? freeink::ui::StateChecked : freeink::ui::StateNormal;
      ++visible;
    }
    freeink::ui::TileGridProps props;
    props.items = tiles;
    props.count = static_cast<uint16_t>(visible);
    props.action = ACTION_ROW;
    props.columns = columns;
    props.gap = gap;
    props.text = screen.theme().bodyText;
    freeink::ui::tileGrid(screen.frame(),
                          {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                           static_cast<int16_t>(std::max(1, bottom - y))},
                          props);
    return;
  }
  screen.setContentMargin({static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                           static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  freeink::ui::ListProps props;
  props.items = rows_;
  props.count = static_cast<uint16_t>(count_);
  props.action = ACTION_ROW;
  props.inputMask = freeink::ui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void LuaAppActivity::activateIndex(int index) {
  if (canvasMode_ && canvas_) {
    if (index >= 0 && static_cast<size_t>(index) < canvas_->controlCount)
      dispatch("on_select", canvas_->commands[canvas_->controls[index]].action);
    return;
  }
  if (index < 0 || static_cast<size_t>(index) >= count_) return;
  app.clearTapFlash();
  dispatch("on_select", index + 1);
}

void LuaAppActivity::title(const char* value) {
  if (!std::strcmp(heading_, value)) return;
  std::snprintf(heading_, sizeof(heading_), "%s", value);
  dirty_ = true;
}
void LuaAppActivity::clear() {
  closeRouting();
  if (nativeService_) nativeService_->clearCanvas();
  if (canvas_) canvas_->clear();
  canvasMode_ = false;
  qrReady_ = false;
  count_ = 0;
  columns_ = 0;
  caption_[0] = 0;
  dirty_ = true;
}
void LuaAppActivity::caption(const char* text) {
  std::snprintf(caption_, sizeof(caption_), "%s", text);
  dirty_ = true;
}
bool LuaAppActivity::row(const char* label, const char* value) {
  if (count_ == crossstore::MAX_ROWS) return false;
  std::snprintf(labels_[count_], sizeof(labels_[0]), "%s", label);
  std::snprintf(values_[count_], sizeof(values_[0]), "%s", value);
  rows_[count_] = {};
  rows_[count_].label = labels_[count_];
  rows_[count_].subtitle = values_[count_];
  rows_[count_].actionValue = static_cast<int16_t>(count_);
  ++count_;
  dirty_ = true;
  return true;
}
bool LuaAppActivity::capability(const char* name) const {
  return (crossstore::deviceCapabilities() & crossstore::capabilityBit(name)) != 0;
}
bool LuaAppActivity::chinese() const { return I18N.getLanguage() == Language::ZH_CN; }
uint32_t LuaAppActivity::milliseconds() const { return millis(); }

bool LuaAppActivity::read(const char* key, char* out, size_t capacity) {
  if (!crossstore::validId(key)) return false;
  char path[128];
  std::snprintf(path, sizeof(path), "/.crosspoint/appdata/%s/%s", id_, key);
  if (!crossstore::recoverFile(Storage, path)) return false;
  auto file = Storage.open(path);
  if (!file || file.size() >= capacity) return false;
  const size_t fileBytes = file.size();
  if (file.read(out, fileBytes) != static_cast<int>(fileBytes)) return false;
  out[fileBytes] = 0;
  return true;
}
bool LuaAppActivity::write(const char* key, const char* value, size_t size) {
  if (!crossstore::validId(key) || size > 2048) return false;
  char dir[96], path[128], temp[136];
  std::snprintf(dir, sizeof(dir), "/.crosspoint/appdata/%s", id_);
  std::snprintf(path, sizeof(path), "%s/%s", dir, key);
  std::snprintf(temp, sizeof(temp), "%s.part", path);
  if (!Storage.exists(dir) && !Storage.mkdir(dir)) return false;
  if (!Storage.exists(path)) {
    auto directory = Storage.open(dir);
    if (!directory || !directory.isDirectory()) return false;
    size_t files = 0;
    while (auto file = directory.openNextFile())
      if (++files >= 32) return false;
  }
  {
    auto file = Storage.open(temp, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file || file.write(value, size) != size) return false;
    file.flush();
  }
  return crossstore::commitFile(Storage, temp, path);
}
void LuaAppActivity::input(const char* title, const char* initial) {
  std::snprintf(inputTitle_, sizeof(inputTitle_), "%s", title);
  std::snprintf(inputText_, sizeof(inputText_), "%s", initial);
  inputPending_ = true;
}
void LuaAppActivity::notify(const char* message) {
  const auto bit = crossstore::permissionBit("notifications");
  if ((package_.permissions & bit) && (permissions_ & bit))
    systemMessages::post(systemMessages::Source::Application, id_, message);
}
bool LuaAppActivity::open(const char* route) {
  if (std::strcmp(route, "library") && std::strcmp(route, "file_transfer") && std::strcmp(route, "opds")) return false;
  if (!std::strcmp(route, "opds") && !allowed("network")) return false;
  std::strcpy(pendingRoute_, route);
  return true;
}

Rect LuaAppActivity::canvasBounds() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, true);
  const int padding = std::max(12, metrics.contentSidePadding);
  const int top = metrics.topPadding + metrics.headerHeight + 8;
  return Rect{safe.x + padding, safe.y + top, std::max(1, safe.width - padding * 2),
              std::max(1, safe.height - top - metrics.buttonHintsHeight - 8)};
}
void LuaAppActivity::size(int& width, int& height) const {
  const auto bounds = canvasBounds();
  width = bounds.width;
  height = bounds.height;
}
bool LuaAppActivity::canvas() {
  if (!canvas_) {
    canvas_ = makeUniqueNoThrow<crossstore::Canvas>();
    if (!canvas_) {
      LOG_ERR("STORE", "OOM: canvas (%u bytes)", static_cast<unsigned>(sizeof(crossstore::Canvas)));
      return false;
    }
  }
  canvasMode_ = true;
  return true;
}
bool LuaAppActivity::draw(const crossstore::DrawCommand& command, const char* text) {
  if (!canvasMode_ || !canvas_) return false;
  const auto bounds = canvasBounds();
  if ((command.kind == crossstore::DrawKind::Button || command.kind == crossstore::DrawKind::Hit) &&
      (command.x < 0 || command.y < 0 || command.x + command.width > bounds.width ||
       command.y + command.height > bounds.height))
    return false;
  dirty_ = true;
  return canvas_->add(command, text);
}
void LuaAppActivity::canvasAction(const freeink::ui::ActionEvent& event, void* user) {
  auto& self = *static_cast<LuaAppActivity*>(user);
  self.app.clearTapFlash();
  int x = -1, y = -1;
  if (event.longPress ? self.mappedInput.wasScreenLongPress(x, y) : self.mappedInput.wasScreenTapped(x, y)) {
    const auto bounds = self.canvasBounds();
    x -= bounds.x;
    y -= bounds.y;
  }
  self.dispatch("on_select", event.value, event.longPress ? "long" : "", x, y);
}
void LuaAppActivity::onBackButton() {
  if (!active_) {
    finish();
    return;
  }
  dispatch("on_back");
  if (!runtime_.handled()) finish();
}
bool LuaAppActivity::handleListSwipe(MappedInputManager::SwipeDir swipe, int x, int y) {
  if (!canvasMode_) return UiListActivity::handleListSwipe(swipe, x, y);
  const char* direction = nullptr;
  switch (swipe) {
    case MappedInputManager::SwipeDir::Left:
      direction = "left";
      break;
    case MappedInputManager::SwipeDir::Right:
      direction = "right";
      break;
    case MappedInputManager::SwipeDir::Up:
      direction = "up";
      break;
    case MappedInputManager::SwipeDir::Down:
      direction = "down";
      break;
    default:
      return false;
  }
  dispatch("on_swipe", 0, direction);
  return true;
}

bool LuaAppActivity::allowed(const char* name) const {
  const auto bit = crossstore::permissionBit(name);
  return (package_.permissions & bit) && (permissions_ & bit);
}
bool LuaAppActivity::sound(const char* cue) { return allowed("sound") && HalBuzzer::play(cue); }

bool LuaAppActivity::asset(const char* name, size_t offset, char* out, size_t capacity, size_t& bytes) {
  if (std::strncmp(name, "assets/", 7) || std::strstr(name, "..") || std::strchr(name, '\\')) return false;
  size_t assetBytes = 0;
  auto data = crossstore::readEntry(crossstore::installedPath(id_), name, 32768, assetBytes);
  if (!data || offset > assetBytes) return false;
  bytes = std::min(assetBytes - offset, capacity);
  std::memcpy(out, data.get() + offset, bytes);
  return true;
}

bool LuaAppActivity::qr(int x, int y, int size, const char* content) {
  const auto bounds = canvasBounds();
  if (!canvas_ || !canvasMode_ || qrReady_ || x < 0 || y < 0 || size < 90 || x + size > bounds.width ||
      y + size > bounds.height)
    return false;
  const size_t bytes = std::strlen(content);
  const uint8_t version = bytes <= 84 ? 5 : bytes <= 256 ? 12 : 16;
  if (bytes > 384 || size < (17 + version * 4 + 8) * 2) return false;
  uint8_t* modules = qrModules_;
  if (version > 5) {
    if (!qrExtended_) {
      // Long login/upload URLs need at most 821 bytes, retained across frames;
      // short identity QR codes keep using the existing inline 172-byte buffer.
      qrExtended_ = makeUniqueNoThrow<uint8_t[]>(qrcode_getBufferSize(16));
      if (!qrExtended_) {
        LOG_ERR("STORE", "OOM allocating extended QR buffer");
        return false;
      }
    }
    modules = qrExtended_.get();
  }
  if (qrcode_initText(&qrCode_, modules, version, ECC_MEDIUM, content) != 0) return false;
  crossstore::DrawCommand command;
  command.kind = crossstore::DrawKind::Qr;
  command.x = x;
  command.y = y;
  command.width = command.height = size;
  qrReady_ = canvas_->add(command, "");
  dirty_ = true;
  return qrReady_;
}

void* LuaAppActivity::resizeAllocation(void* ptr, size_t bytes) {
  return vmPsram_ ? HalMemory::resizePsram(ptr, bytes) : crossstore::Host::resizeAllocation(ptr, bytes);
}
