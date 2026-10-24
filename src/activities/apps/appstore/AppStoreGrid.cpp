#include <FontCacheManager.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <optional>

#include "AppStoreActivity.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;
namespace {
fui::Rect uiRect(const Rect& value) {
  return {static_cast<int16_t>(value.x), static_cast<int16_t>(value.y), static_cast<int16_t>(value.width),
          static_cast<int16_t>(value.height)};
}
}  // namespace

crossstore::StoreGrid AppStoreActivity::geometry() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, true);
  safe.y += metrics.topPadding;
  safe.height = std::max(1, safe.height - metrics.topPadding);
  const auto fonts = uiScaleSpec(false);
  return crossstore::StoreGrid::fit(safe, renderer.getLineHeight(fonts.bodyFontId),
                                    renderer.getLineHeight(fonts.smallFontId), metrics.tabBarHeight,
                                    metrics.contentSidePadding);
}
Rect AppStoreActivity::listBounds() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, true);
  const int top = std::max(safe.y, metrics.topPadding + metrics.headerHeight);
  return Rect{safe.x, top, safe.width, std::max(1, safe.y + safe.height - top)};
}
void AppStoreActivity::screenCallback(UiScreen& screen, void* self) {
  static_cast<AppStoreActivity*>(self)->buildScreen(screen);
}
void AppStoreActivity::rowCallback(const fui::ActionEvent& event, void* self) {
  auto& store = *static_cast<AppStoreActivity*>(self);
  if (store.view_ == View::Browser) return;
  {
    RenderLock lock;
    store.nav_.selected = event.value;
  }
  store.activateIndex(event.value);
}
void AppStoreActivity::appCallback(const fui::ActionEvent& event, void* self) {
  auto& store = *static_cast<AppStoreActivity*>(self);
  if (store.view_ != View::Browser || event.value < 0 || static_cast<size_t>(event.value) >= store.count_) return;
  {
    RenderLock lock;
    store.app.clearTapFlash();
    store.focus_ = event.value + 3;
  }
  store.details(event.value);
}
void AppStoreActivity::controlCallback(const fui::ActionEvent& event, void* self) {
  auto& store = *static_cast<AppStoreActivity*>(self);
  if (store.view_ != View::Browser || event.value < static_cast<int>(Control::All) ||
      event.value > static_cast<int>(Control::Refresh))
    return;
  store.app.clearTapFlash();
  store.control(static_cast<Control>(event.value));
}
void AppStoreActivity::control(Control action) {
  switch (action) {
    case Control::All:
      switchTab(Tab::All);
      break;
    case Control::Installed:
      switchTab(Tab::Installed);
      break;
    case Control::Settings:
      settings();
      break;
    case Control::Previous:
      turnPage(-1);
      break;
    case Control::Next:
      turnPage(1);
      break;
    case Control::Refresh:
      refresh(true);
      break;
  }
}
void AppStoreActivity::stepFocus(int direction) {
  if (view_ == View::ConfirmInstall || view_ == View::ConfirmRemove) {
    stepConfirmationFocus(direction);
    return;
  }
  if (view_ == View::Details) {
    stepDetailsFocus(direction);
    return;
  }
  RenderLock lock;
  keyboardFocus_ = true;
  if (view_ == View::Browser) {
    // Header controls, visible apps, previous/next, refresh form one keyboard
    // ring. All controls remain reachable on a two-direction device.
    const int count = static_cast<int>(count_) + 6;
    focus_ = (focus_ + direction + count) % count;
  } else {
    const int count = static_cast<int>(rowCount_);
    if (count) nav_.requestSelection((nav_.selected + direction + count) % count);
  }
  requestUpdate();
}
void AppStoreActivity::activateFocus() {
  if (view_ == View::ConfirmInstall || view_ == View::ConfirmRemove) {
    if (primaryFocused_) confirmationAction(focus_);
    return;
  }
  if (view_ == View::Details) {
    activateDetailsFocus();
    return;
  }
  if (view_ != View::Browser) {
    activateIndex(nav_.selected);
    return;
  }
  if (focus_ < 3) {
    control(static_cast<Control>(focus_));
    return;
  }
  if (static_cast<size_t>(focus_ - 3) < count_) {
    details(focus_ - 3);
    return;
  }
  const int tail = focus_ - static_cast<int>(count_) - 3;
  if (tail >= 0 && tail <= 2) control(static_cast<Control>(static_cast<int>(Control::Previous) + tail));
}
void AppStoreActivity::guardHeldButtons() {
  using Button = MappedInputManager::Button;
  releasePending_ = mappedInput.isPressed(Button::Confirm) || mappedInput.isPressed(Button::Back) ||
                    mappedInput.isPressed(Button::NavNext) || mappedInput.isPressed(Button::NavPrevious);
}
bool AppStoreActivity::consumeHeldButtons() {
  if (!releasePending_) return false;
  guardHeldButtons();
  // Also consume the frame that clears the inherited hold. Touch still routes.
  return true;
}
bool AppStoreActivity::inputActive() const {
  int x = 0, y = 0;
  if (mappedInput.wasScreenTouchPressed(x, y) || mappedInput.isScreenTouchHeld(x, y) ||
      mappedInput.wasScreenTouchReleased())
    return true;
  using Button = MappedInputManager::Button;
  constexpr std::array keys{Button::Confirm, Button::Back, Button::NavNext, Button::NavPrevious};
  return std::any_of(keys.begin(), keys.end(), [this](const auto key) {
    return mappedInput.isPressed(key) || mappedInput.wasPressed(key) || mappedInput.wasReleased(key);
  });
}
void AppStoreActivity::loop() {
  pumpRefresh();
  if (view_ == View::Browser && !inputActive() && pageSize_ != static_cast<size_t>(geometry().capacity)) {
    browser(true);
    return;
  }
  const bool blocked = consumeHeldButtons();
  if (!blocked && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return;
  }
  if (!blocked && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateFocus();
    return;
  }
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return;
  if (!blocked && BoardConfig::isPaperMono() && view_ == View::Browser) {
    using Button = MappedInputManager::Button;
    const bool up = mappedInput.wasPressed(Button::Up);
    const bool down = mappedInput.wasPressed(Button::Down);
    if (up || down) {
      guardHeldButtons();
      if (up != down) turnPage(up ? -1 : 1);
      return;
    }
    if (mappedInput.isPressed(Button::Up) || mappedInput.isPressed(Button::Down) ||
        mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Down))
      return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (view_ == View::Browser) {
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) {
      turnPage(1);
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) {
      turnPage(-1);
      return;
    }
  } else if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    if (view_ == View::Details || view_ == View::ConfirmInstall || view_ == View::ConfirmRemove) {
      scrollDetails(swipe == MappedInputManager::SwipeDir::Up ? nav_.inputPageRows() : -nav_.inputPageRows());
      return;
    }
    RenderLock lock;
    nav_.requestScroll(swipe == MappedInputManager::SwipeDir::Up ? nav_.inputPageRows() : -nav_.inputPageRows());
    requestUpdate();
    return;
  }
  if (!blocked) {
    buttons_.onNextRelease([this] { stepFocus(1); });
    buttons_.onPreviousRelease([this] { stepFocus(-1); });
  }
}
void AppStoreActivity::buildGrid(UiScreen& screen) {
  const auto layout = geometry();
  auto bodyText = screen.theme().bodyText;
  bodyText.align = fui::TextAlign::Center;
  auto small = screen.theme().smallText;
  small.align = fui::TextAlign::Center;
  const auto header = [&](Rect rect, Control control, const char* label, bool selected, int focus) {
    if (label)
      screen.target().text(uiRect(rect), label, bodyText);
    else
      screen.target().bitmap(uiRect(rect), listIconFor(UIIcon::Settings, 32), fui::BitmapMode::Center,
                             fui::Paint::solid(fui::Color::Black));
    screen.frame().hit(uiRect(rect), ACTION_CONTROL, static_cast<int16_t>(control), fui::InputTouch);
    if (selected)
      renderer.drawLine(rect.x + 8, rect.y + rect.height - 3, rect.x + rect.width - 9, rect.y + rect.height - 3, 2,
                        true);
    if (keyboardFocus_ && focus_ == focus)
      renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 8, true);
  };
  header(layout.all, Control::All, tr(STR_STORE_ALL), tab_ == Tab::All, 0);
  header(layout.installed, Control::Installed, tr(STR_INSTALLED), tab_ == Tab::Installed, 1);
  header(layout.settings, Control::Settings, nullptr, false, 2);
  screen.target().text(uiRect(layout.status), statusText_, small);
  screen.frame().hit(uiRect(layout.status), ACTION_CONTROL, static_cast<int16_t>(Control::Refresh), fui::InputTouch);
  if (keyboardFocus_ && focus_ == static_cast<int>(count_) + 5)
    renderer.drawRect(layout.status.x, layout.status.y, layout.status.width, layout.status.height, true);
  const int lineHeight = screen.target().lineHeight(bodyText.font);
  for (size_t i = 0; i < count_ && i < static_cast<size_t>(layout.capacity); ++i) {
    const Rect cell = layout.cell(i);
    if (keyboardFocus_ && focus_ == static_cast<int>(i) + 3)
      renderer.drawRoundedRect(cell.x, cell.y, cell.width, cell.height, 2, 10, true);
    screen.frame().hit(uiRect(cell), ACTION_APP, static_cast<int16_t>(i), fui::InputTouch);
    const int iconSize = crossstore::StoreGrid::iconSize(cell, lineHeight, renderer.getGrayscaleLevels());
    const int iconX = cell.x + (cell.width - iconSize) / 2;
    const int iconY = cell.y + std::max(4, (cell.height - lineHeight * 2 - iconSize - 8) / 2);
    icons_.paint(renderer, static_cast<int>(i), iconX, iconY, iconSize, marks_[i]);
    const Rect label{cell.x + 5, iconY + iconSize + 6, std::max(1, cell.width - 10), lineHeight * 2};
    auto nameStyle = bodyText;
    nameStyle.maxLines = 2;
    screen.target().text(uiRect(label), entries_[i].package.name, nameStyle);
  }
  if (!count_) {
    screen.target().text(uiRect(layout.body),
                         tab_ == Tab::Installed ? tr(STR_STORE_EMPTY_INSTALLED)
                         : catalog_.hasCache()  ? tr(STR_STORE_EMPTY)
                                                : tr(STR_STORE_FIRST_LOAD),
                         bodyText);
  }
  const auto footer = [&](Rect rect, Control control, const char* text, bool enabled, int focus) {
    auto style = small;
    if (!enabled) style.color = fui::Color::DarkGray;
    screen.target().text(uiRect(rect), text, style);
    if (enabled) screen.frame().hit(uiRect(rect), ACTION_CONTROL, static_cast<int16_t>(control), fui::InputTouch);
    if (keyboardFocus_ && focus_ == focus)
      renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 8, true);
  };
  const size_t offset = offsets_[static_cast<unsigned>(tab_)];
  footer(layout.previous, Control::Previous, tr(STR_STORE_PREVIOUS), offset > 0, static_cast<int>(count_) + 3);
  footer(layout.next, Control::Next, tr(STR_STORE_NEXT), offset + count_ < total_, static_cast<int>(count_) + 4);
  screen.target().text(uiRect(layout.page), pageText_, small);
}
void AppStoreActivity::buildScreen(UiScreen& screen) {
  icons_.resetPlacements();
  if (view_ == View::Browser) {
    buildGrid(screen);
    return;
  }
  if (view_ == View::ConfirmInstall || view_ == View::ConfirmRemove) {
    buildConfirmation(screen);
    return;
  }
  if (view_ == View::Details) {
    buildDetails(screen);
    return;
  }
  const auto bounds = listBounds();
  screen.setContentMarginFromScreen(
      {static_cast<int16_t>(bounds.y), static_cast<int16_t>(renderer.getScreenWidth() - bounds.x - bounds.width),
       static_cast<int16_t>(renderer.getScreenHeight() - bounds.y - bounds.height), static_cast<int16_t>(bounds.x)});
  fui::ListProps props;
  props.items = rows_;
  props.count = static_cast<uint16_t>(rowCount_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  if (!mappedInput.hasTouch()) props.rowHeight = UITheme::getInstance().getMetrics().listRowHeight;
  screen.syncListViewport(nav_, props, static_cast<int>(rowCount_));
  screen.list(props);
}
void AppStoreActivity::drawChrome() {
  if (view_ == View::Browser) {
    HeaderBackTapTarget::clear();
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 view_ == View::Settings         ? tr(STR_STORE_SETTINGS)
                 : view_ == View::ConfirmInstall ? tr(STR_STORE_CONFIRM_DOWNLOAD)
                 : view_ == View::ConfirmRemove  ? tr(STR_STORE_CONFIRM_REMOVE)
                                                 : tr(STR_APPSTORE));
}
void AppStoreActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
void AppStoreActivity::render(RenderLock&&) {
  std::optional<FontCacheManager::PrewarmScope> prewarm;
  if (auto* fonts = renderer.getFontCacheManager(); fonts && !fonts->isScanning()) {
    prewarm.emplace(fonts->createUiCjkPrewarmScope());
    drawChrome();
    scanUi();
    for (int pass = 0; nav_.consumeRebuildNeeded() && pass < 8; ++pass) scanUi();
    drawFooter();
    prewarm->endScanAndPrewarm();
  }
  if (view_ == View::Browser)
    icons_.beginFrame(renderer);
  else
    renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; view_ != View::Browser && nav_.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  drawFooter();
  if (view_ == View::Browser)
    icons_.displayFrame(renderer);
  else
    renderer.displayBuffer();
}
