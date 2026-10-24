#include <algorithm>

#include "AppStoreActivity.h"
#include "components/UITheme.h"
#include "crossstore/StoreDetails.h"

namespace fui = freeink::ui;
namespace {
fui::Rect uiRect(const Rect& value) {
  return {static_cast<int16_t>(value.x), static_cast<int16_t>(value.y), static_cast<int16_t>(value.width),
          static_cast<int16_t>(value.height)};
}
}  // namespace

size_t AppStoreActivity::detailListCount() const {
  return primaryRow_ >= 0 ? static_cast<size_t>(primaryRow_) : rowCount_;
}

void AppStoreActivity::detailCallback(const fui::ActionEvent& event, void* self) {
  auto& store = *static_cast<AppStoreActivity*>(self);
  if (store.view_ != View::Details || event.value < 0 || static_cast<size_t>(event.value) >= store.rowCount_ ||
      !store.rows_[event.value].enabled)
    return;
  {
    RenderLock lock;
    store.app.clearTapFlash();
    store.primaryFocused_ = event.value == store.primaryRow_;
    if (store.primaryFocused_) {
      store.nav_.selected = -1;
      store.nav_.followOnBuild = false;
    } else {
      store.nav_.requestSelection(event.value);
    }
  }
  store.activateIndex(event.value);
}

void AppStoreActivity::stepDetailsFocus(int direction) {
  RenderLock lock;
  keyboardFocus_ = true;
  const int count = static_cast<int>(detailListCount());
  const int slots = count + (primaryRow_ >= 0 ? 1 : 0);
  if (!slots) return;
  const int current = primaryFocused_ || !count ? count : std::clamp(nav_.selected.load(), 0, count - 1);
  const int next = (current + direction + slots) % slots;
  primaryFocused_ = primaryRow_ >= 0 && next == count;
  if (primaryFocused_) {
    nav_.selected = -1;
    nav_.followOnBuild = false;
  } else {
    nav_.requestSelection(next);
  }
  requestUpdate();
}

void AppStoreActivity::activateDetailsFocus() {
  const int selected = primaryFocused_ ? primaryRow_ : nav_.selected.load();
  if (selected >= 0 && static_cast<size_t>(selected) < rowCount_ && rows_[selected].enabled) activateIndex(selected);
}

void AppStoreActivity::scrollDetails(int rows) {
  RenderLock lock;
  // Scrolling reads the description; it does not move the confirmation focus
  // onto whichever action happens to enter the viewport.
  nav_.requestScroll(rows);
  requestUpdate();
}

void AppStoreActivity::buildDetails(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  const auto layout = crossstore::StoreDetails::fit(listBounds(), screen.target().lineHeight(theme.bodyText.font),
                                                    theme.minTouchSize, metrics.contentSidePadding, primaryRow_ >= 0);
  screen.setContentMarginFromScreen(
      {static_cast<int16_t>(layout.list.y),
       static_cast<int16_t>(renderer.getScreenWidth() - layout.list.x - layout.list.width),
       static_cast<int16_t>(renderer.getScreenHeight() - layout.list.y - layout.list.height),
       static_cast<int16_t>(layout.list.x)});
  fui::ListProps props;
  props.items = rows_;
  props.count = static_cast<uint16_t>(detailListCount());
  props.action = ACTION_DETAIL;
  props.inputMask = fui::InputTouch;
  props.rowHeight = std::max({44, static_cast<int>(theme.minTouchSize), metrics.listRowHeight});
  props.rowGap = crossstore::StoreDetails::GAP;
  props.rowStyles = theme.listRow.unset() ? fui::defaultListRowStyles() : theme.listRow;
  // Non-interactive information remains full-contrast. The keyboard marker
  // still identifies its current row while the viewport follows that row.
  props.rowStyles.disabled = props.rowStyles.normal;
  props.selectionMarker = fui::SelectionMarker::Underline;
  screen.syncListViewport(nav_, props, static_cast<int>(detailListCount()));
  screen.list(props);
  if (primaryRow_ < 0) return;

  fui::ButtonProps button;
  button.label = labels_[primaryRow_];
  button.action = ACTION_DETAIL;
  button.value = static_cast<int16_t>(primaryRow_);
  button.inputMask = fui::InputTouch;
  button.text = theme.bodyText;
  button.text.maxLines = 2;
  button.radius = 8;
  button.styles = theme.button.unset() ? fui::defaultButtonStyles() : theme.button;
  auto solid = button.styles.normal;
  solid.background = fui::Paint::solid(fui::Color::Black);
  solid.foreground = fui::Paint::solid(fui::Color::White);
  button.styles.normal = button.styles.selected = button.styles.focused = button.styles.active = solid;
  // A white border makes physical-key focus visible without changing the
  // solid primary-button treatment or its shared draw/hit rectangle.
  if (keyboardFocus_ && primaryFocused_) {
    button.styles.normal.border = fui::Paint::solid(fui::Color::White);
    button.styles.normal.borderWidth = 2;
  }
  screen.button(button, uiRect(layout.primary));
}
