#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "AppStoreActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "crossstore/Permissions.h"
#include "crossstore/StoreConfirmation.h"
#include "crossstore/Uninstall.h"

namespace fui = freeink::ui;
namespace {
fui::Rect uiRect(const Rect& rect) {
  return {static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
          static_cast<int16_t>(rect.height)};
}
}  // namespace

void AppStoreActivity::showInstallConfirmation() {
  RenderLock lock;
  resetRows(View::ConfirmInstall);
  const auto& entry = entries_[chosen_];
  addRow(entry.package.name, entry.package.version);
  char size[96];
  std::snprintf(size, sizeof(size), tr(STR_STORE_BYTES_FORMAT), static_cast<unsigned>((entry.bytes + 1023) / 1024),
                static_cast<unsigned>(entry.bytes));
  addRow(tr(STR_STORE_DOWNLOAD_SIZE), size);
  addRow(tr(STR_STORE_PERMISSIONS),
         entry.package.permissions ? tr(STR_STORE_PERMISSION_CONFIRM_HINT) : tr(STR_STORE_PERMISSION_NONE));
  for (const auto& permission : crossstore::PERMISSIONS)
    if (entry.package.permissions & permission.bit) addRow(I18N.get(permission.title));
  for (size_t i = 0; i < rowCount_; ++i) rows_[i].enabled = false;
  primaryFocused_ = true;
  nav_.selected = -1;
  nav_.followOnBuild = false;
  focus_ = 1;  // A second inherited Confirm must never authorize a download.
  guardHeldButtons();
  requestUpdate();
}

void AppStoreActivity::showRemoveConfirmation() {
  RenderLock lock;
  resetRows(View::ConfirmRemove);
  addRow(entries_[chosen_].package.name, entries_[chosen_].package.version);
  addRow(tr(STR_STORE_DELETE_KEEP), tr(STR_STORE_DELETE_KEEP_HINT));
  addRow(tr(STR_STORE_DELETE_ALL), tr(STR_STORE_DELETE_ALL_HINT));
  addRow(tr(STR_STORE_SHARED_DATA), tr(STR_STORE_SHARED_DATA_HINT));
  for (size_t i = 0; i < rowCount_; ++i) rows_[i].enabled = false;
  primaryFocused_ = true;
  nav_.selected = -1;
  nav_.followOnBuild = false;
  focus_ = 2;
  guardHeldButtons();
  requestUpdate();
}

void AppStoreActivity::confirmationCallback(const fui::ActionEvent& event, void* self) {
  auto& store = *static_cast<AppStoreActivity*>(self);
  if (store.view_ != View::ConfirmInstall && store.view_ != View::ConfirmRemove) return;
  store.confirmationAction(event.value);
}

void AppStoreActivity::stepConfirmationFocus(int direction) {
  RenderLock lock;
  keyboardFocus_ = true;
  const int actions = view_ == View::ConfirmRemove ? 3 : 2;
  const int rows = static_cast<int>(rowCount_);
  const int current = primaryFocused_ ? rows + focus_ : std::clamp(nav_.selected.load(), 0, rows - 1);
  const int next = (current + direction + rows + actions) % (rows + actions);
  primaryFocused_ = next >= rows;
  if (primaryFocused_) {
    focus_ = next - rows;
    nav_.selected = -1;
    nav_.followOnBuild = false;
  } else
    nav_.requestSelection(next);
  requestUpdate();
}

void AppStoreActivity::confirmationAction(int action) {
  const auto view = view_;
  if (view != View::ConfirmInstall && view != View::ConfirmRemove) return;
  const int cancel = view == View::ConfirmRemove ? 2 : 1;
  if (action < 0 || action > cancel) return;
  if (action == cancel) {
    details(chosen_);
    return;
  }
  if (view == View::ConfirmInstall)
    confirmInstall();
  else
    removeConfirmed(action == 1);
}

void AppStoreActivity::confirmInstall() {
  stopRefresh();
  if (WiFi.status() == WL_CONNECTED) {
    install();
    return;
  }
  wifiStarted_ = wifiStarted_ || WiFi.getMode() == WIFI_OFF;
  startActivityForResultWith<WifiSelectionActivity>([this](const ActivityResult& result) {
    guardHeldButtons();
    if (!result.isCancelled) install();
  });
}

void AppStoreActivity::removeConfirmed(bool includeData) {
  stopRefresh();
  if (!crossstore::uninstallPackage(entries_[chosen_].package.id, includeData)) {
    fail(tr(STR_STORE_REMOVE_INCOMPLETE));
    return;
  }
  reloadInstalled();
  details(chosen_);
}

void AppStoreActivity::buildConfirmation(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool removing = view_ == View::ConfirmRemove;
  const int actions = removing ? 3 : 2;
  const auto layout = crossstore::StoreConfirmation::fit(listBounds(), screen.target().lineHeight(theme.bodyText.font),
                                                         theme.minTouchSize, metrics.contentSidePadding, actions);
  screen.setContentMarginFromScreen(
      {static_cast<int16_t>(layout.list.y),
       static_cast<int16_t>(renderer.getScreenWidth() - layout.list.x - layout.list.width),
       static_cast<int16_t>(renderer.getScreenHeight() - layout.list.y - layout.list.height),
       static_cast<int16_t>(layout.list.x)});
  fui::ListProps props;
  props.items = rows_;
  props.count = static_cast<uint16_t>(rowCount_);
  props.inputMask = fui::InputTouch;
  props.rowHeight = std::max({44, static_cast<int>(theme.minTouchSize), metrics.listRowHeight});
  props.rowGap = crossstore::StoreConfirmation::GAP;
  props.rowStyles = theme.listRow.unset() ? fui::defaultListRowStyles() : theme.listRow;
  props.rowStyles.disabled = props.rowStyles.normal;
  props.selectionMarker = fui::SelectionMarker::Underline;
  screen.syncListViewport(nav_, props, static_cast<int>(rowCount_));
  if (primaryFocused_ || !keyboardFocus_) props.selectedIndex = -1;
  screen.list(props);

  for (int i = 0; i < actions; ++i) {
    fui::ButtonProps button;
    button.label = i == actions - 1 ? tr(STR_CANCEL)
                   : removing       ? (i == 0 ? tr(STR_STORE_DELETE_KEEP) : tr(STR_STORE_DELETE_ALL))
                   : entries_[chosen_].package.permissions ? tr(STR_STORE_CONFIRM_ALLOW_DOWNLOAD)
                                                           : tr(STR_STORE_CONFIRM_DOWNLOAD);
    button.action = ACTION_CONFIRM;
    button.value = static_cast<int16_t>(i);
    button.inputMask = fui::InputTouch;
    button.text = layout.buttons[i].width < layout.list.width / 2 ? theme.smallText : theme.bodyText;
    button.text.maxLines = 2;
    button.radius = 8;
    button.styles = theme.button.unset() ? fui::defaultButtonStyles() : theme.button;
    auto style = button.styles.normal;
    const bool solid = !removing && i == 0;
    style.background = fui::Paint::solid(solid ? fui::Color::Black : fui::Color::White);
    style.foreground = fui::Paint::solid(solid ? fui::Color::White : fui::Color::Black);
    style.border = fui::Paint::solid(fui::Color::Black);
    style.borderWidth = 1;
    button.styles.normal = button.styles.active = button.styles.selected = button.styles.focused = style;
    screen.button(button, uiRect(layout.buttons[i]));
    if (keyboardFocus_ && primaryFocused_ && focus_ == i)
      renderer.drawRoundedRect(layout.buttons[i].x + 4, layout.buttons[i].y + 4, layout.buttons[i].width - 8,
                               layout.buttons[i].height - 8, 2, 6, !solid);
  }
}
