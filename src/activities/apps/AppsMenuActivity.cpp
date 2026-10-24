#include "AppsMenuActivity.h"

#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <numeric>
#include <string>

#include "AppCapabilities.h"
#include "ChatBuildConfig.h"
#include "CrossPointSettings.h"
#include "InxItemLayout.h"
#include "OpdsServerStore.h"
#include "appstore/AppStoreActivity.h"
#include "appstore/LuaAppActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "crossstore/Package.h"
#include "crossstore/StoreIcon.generated.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
int iconSizeForCell(const Rect& cell, int lineHeight, int levels) {
  if (levels == 16 && cell.height >= 96 + lineHeight + 18 && cell.width >= 108) return 96;
  return cell.height >= 64 + lineHeight + 18 && cell.width >= 76 ? 64 : 32;
}
}  // namespace

int AppsMenuActivity::getAppCount() { return 1; }
StrId AppsMenuActivity::getAppTitleId(int) { return StrId::STR_APPSTORE; }
bool AppsMenuActivity::isAppVisible(int index) { return index == 0; }
bool AppsMenuActivity::isAppSupported(int index) { return index == 0; }
bool AppsMenuActivity::setAppVisible(int, bool) { return false; }

void AppsMenuActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  if (BoardConfig::isPaperMono() && usesIconLayout()) {
    turnIconPage(edge == MainTabContentEdge::First ? 1 : -1);
    return;
  }
  moveSelectionTo(MainTabs::contentEdgeIndex(edge, getVisibleAppCount()));
}
void AppsMenuActivity::onEnter() {
  UiListActivity::onEnter();
  guardGridButtons();
  reloadInstalled();
}
void AppsMenuActivity::onExit() {
  icons.clear();
  rowItems.clear();
  Activity::onExit();
}
void AppsMenuActivity::loop() {
  UiListActivity::loop();
  if (iconCacheStart != nav.top) {
    RenderLock lock;
    loadPageIcons();
    requestUpdate();
  }
}
void AppsMenuActivity::guardGridButtons() {
  using Button = MappedInputManager::Button;
  gridButtonReleasePending = mappedInput.isPressed(Button::Up) || mappedInput.isPressed(Button::Down);
}
bool AppsMenuActivity::handleCustomInput() {
  if (!BoardConfig::isPaperMono() || !usesIconLayout()) return false;
  using Button = MappedInputManager::Button;
  const bool up = mappedInput.wasPressed(Button::Up);
  const bool down = mappedInput.wasPressed(Button::Down);
  const bool blocked = gridButtonReleasePending;
  if (blocked) guardGridButtons();
  const bool active = up || down || mappedInput.isPressed(Button::Up) || mappedInput.isPressed(Button::Down) ||
                      mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Down);
  if (!blocked && !active) return false;
  // Only physical navigation is claimed. Independent touch remains usable
  // while an inherited or already-handled page key is held.
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return true;
  int x = 0, y = 0;
  const auto swipe = mappedInput.wasSwipe(&x, &y);
  if (handleListSwipe(swipe, x, y)) return true;
  if (blocked) return true;
  if (up || down) {
    guardGridButtons();
    if (up != down) turnIconPage(up ? -1 : 1);
  }
  // No row-selection or continuous-navigation fallback for a page-key hold.
  return true;
}
void AppsMenuActivity::turnIconPage(const int direction) {
  RenderLock lock;
  constexpr int capacity = InxGridGeometry::itemsPerPage;
  const int lastPage = (getVisibleAppCount() - 1) / capacity;
  const int page = std::clamp(nav.top / capacity + direction, 0, lastPage);
  const int next = page * capacity;
  if (next == nav.top) return;
  // Set the viewport and icon cache together before publishing the next frame.
  // The existing per-page icon budget is reused; no extra page is retained.
  nav.requestSelection(next);
  nav.top = next;
  clearContentSelection();
  closeRouting();
  loadPageIcons();
  requestUpdate();
}
bool AppsMenuActivity::handleListSwipe(const MappedInputManager::SwipeDir swipe, const int x, const int y) {
  if (BoardConfig::isPaperMono() && usesIconLayout() &&
      (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down)) {
    turnIconPage(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return true;
  }
  return UiListActivity::handleListSwipe(swipe, x, y);
}
const char* AppsMenuActivity::appTitle(int index) const {
  return index >= 0 && static_cast<size_t>(index) < installedCount ? installedApps[index].name : tr(STR_APPSTORE);
}
void AppsMenuActivity::reloadInstalled() {
  RenderLock lock;
  closeRouting();
  crossstore::recoverInstalls();
  installedCount = 0;
  auto dir = Storage.open(crossstore::INSTALL_DIR);
  if (dir && dir.isDirectory()) {
    while (auto file = dir.openNextFile()) {
      char filename[64]{};
      file.getName(filename, sizeof(filename));
      if (!crossstore::installedFilename(filename)) continue;
      crossstore::Package package;
      const std::string path = std::string(crossstore::INSTALL_DIR) + "/" + filename;
      if (!crossstore::readPackage(path, package) || path != crossstore::installedPath(package.id)) continue;
      if (installedCount == MAX_INSTALLED) break;
      auto& entry = installedApps[installedCount++];
      std::snprintf(entry.id, sizeof(entry.id), "%s", package.id);
      std::snprintf(entry.name, sizeof(entry.name), "%s", package.name);
    }
  }
  std::sort(installedApps.begin(), installedApps.begin() + installedCount,
            [](const auto& a, const auto& b) { return std::strcmp(a.name, b.name) < 0; });
  nav.top = std::min(nav.top, std::max(0, getVisibleAppCount() - 1));
  rebuildRowItems();
  loadPageIcons();
  requestUpdate();
}
void AppsMenuActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(installedCount + 1);
  for (int index = 0; index < getVisibleAppCount(); ++index) {
    fui::ListItem item;
    item.label = appTitle(index);
    item.icon = listIconFor(UIIcon::Apps, 32);
    item.actionValue = static_cast<int16_t>(index);
    rowItems.push_back(item);
  }
}
void AppsMenuActivity::loadPageIcons() {
  iconCacheStart = nav.top;
  icons.clear();
  const bool grid = usesIconLayout();
  const auto rect = appContentRect();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int levels = renderer.getGrayscaleLevels();
  for (size_t slot = 0; slot < crossstore::PackageIcons::CAPACITY; ++slot) {
    const size_t index = static_cast<size_t>(iconCacheStart) + slot;
    if (index > installedCount) break;
    const auto cell = InxGridGeometry::cellBounds(static_cast<int>(slot), rect.width, rect.height);
    const int size = grid ? iconSizeForCell(cell, lineHeight, levels) : 32;
    if (index == installedCount) {
      icons.loadEmbeddedBundle(slot, crossstore::builtin::STORE_ICON, sizeof(crossstore::builtin::STORE_ICON), levels,
                               size);
    } else {
      icons.loadPackage(slot, crossstore::installedPath(installedApps[index].id).c_str(), levels, size);
    }
  }
}

bool AppsMenuActivity::usesIconLayout() const {
  return InxGridGeometry::layoutFrom(SETTINGS.inxAppsLayout) == InxItemLayout::Icons;
}

Rect AppsMenuActivity::appContentRect() const {
  const int spacing = UITheme::getInstance().getMetrics().verticalSpacing;
  Rect content = pageContentRect();
  content.y += spacing;
  content.height -= spacing * 2;
  return content;
}

void AppsMenuActivity::openSelected() {
  const int appIndex = getAppIndexForVisibleIndex(nav.selected);
  if (appIndex < 0) return;
  if (static_cast<size_t>(appIndex) < installedCount)
    startActivityForResultWith<LuaAppActivity>([this](const ActivityResult&) { guardGridButtons(); },
                                               installedApps[appIndex].id);
  else
    startActivityForResultWith<AppStoreActivity>([this](const ActivityResult&) {
      guardGridButtons();
      reloadInstalled();
    });
}

void AppsMenuActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;
  openSelected();
}

void AppsMenuActivity::drawIconGrid(UiScreen& screen, const Rect& rect, const int visibleCount,
                                    const bool showSelection) {
  const int start = nav.top;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  for (int slot = 0; slot < InxGridGeometry::itemsPerPage && start + slot < visibleCount; ++slot) {
    const int visibleIndex = start + slot;
    const int appIndex = getAppIndexForVisibleIndex(visibleIndex);
    if (appIndex < 0) continue;
    const auto bounds = InxGridGeometry::cellBounds(slot, rect.width, rect.height);
    const Rect cell{rect.x + bounds.x, rect.y + bounds.y, bounds.width, bounds.height};
    const int iconSize = iconSizeForCell(cell, lineHeight, renderer.getGrayscaleLevels());
    const bool isSelected = showSelection && visibleIndex == nav.selected;
    if (isSelected) renderer.drawRoundedRect(cell.x + 2, cell.y + 2, cell.width - 4, cell.height - 4, 2, 10, true);
    screen.frame().hit(fui::Rect{static_cast<int16_t>(cell.x), static_cast<int16_t>(cell.y),
                                 static_cast<int16_t>(cell.width), static_cast<int16_t>(cell.height)},
                       ACTION_ROW, static_cast<int16_t>(visibleIndex), fui::InputTouch);

    const int iconX = cell.x + (cell.width - iconSize) / 2;
    const int iconY = cell.y + std::max(5, (cell.height - iconSize - lineHeight - 8) / 2);
    paintIcon(appIndex, iconX, iconY, iconSize);
    auto text = screen.theme().bodyText;
    text.align = fui::TextAlign::Center;
    text.bold = false;
    text.color = fui::Color::Black;
    screen.target().text(fui::Rect{static_cast<int16_t>(cell.x + 4), static_cast<int16_t>(iconY + iconSize + 8),
                                   static_cast<int16_t>(std::max(1, cell.width - 8)), static_cast<int16_t>(lineHeight)},
                         appTitle(appIndex), text);
  }

  GUI.drawSideScrollBar(renderer, rect, visibleCount, start, InxGridGeometry::itemsPerPage);
}

void AppsMenuActivity::buildScreen(UiScreen& screen) {
  icons.resetPlacements();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const Rect content = appContentRect();
  const int visibleCount = getVisibleAppCount();
  const bool showSelection = showMainTabContentSelection();

  if (usesIconLayout()) {
    syncPagedViewport(InxGridGeometry::itemsPerPage, BoardConfig::isPaperMono());
    drawIconGrid(screen, content, visibleCount, showSelection);
  } else {
    screen.setContentMarginFromScreen(
        fui::Insets{static_cast<int16_t>(content.y), static_cast<int16_t>(sw - content.x - content.width),
                    static_cast<int16_t>(sh - content.y - content.height), static_cast<int16_t>(content.x)});
    fui::ListProps props;
    props.items = rowItems.data();
    props.count = static_cast<uint16_t>(rowItems.size());
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    props.iconPainter = paintListIcon;
    props.iconPainterCtx = this;
    syncListViewport(screen, props);
    screen.list(props);
  }
}

void AppsMenuActivity::paintListIcon(void* ctx, const fui::Rect& rect, const fui::ListItem& item, const fui::Paint) {
  auto& self = *static_cast<AppsMenuActivity*>(ctx);
  const int index = self.getAppIndexForVisibleIndex(item.actionValue);
  if (index < 0) return;
  const int size = std::min(rect.width, rect.height);
  self.paintIcon(index, rect.x + (rect.width - size) / 2, rect.y + (rect.height - size) / 2, size);
}

void AppsMenuActivity::paintIcon(const int appIndex, const int x, const int y, const int size) {
  icons.paint(renderer, appIndex - iconCacheStart, x, y, size);
}

void AppsMenuActivity::beginDisplayFrame() { icons.beginFrame(renderer); }

void AppsMenuActivity::displayFrame() { icons.displayFrame(renderer); }

void AppsMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  drawPageHeader(Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, tr(STR_APPS_TITLE));
}

void AppsMenuActivity::drawFooter() {
  const int visibleCount = getVisibleAppCount();
  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_SELECT), visibleCount > 1);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void AppsMenuActivity::onBackButton() { activityManager.goHome(); }
