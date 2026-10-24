#include <I18n.h>
#include <WiFi.h>

#include <cstring>

#include "AppStoreActivity.h"
#include "NetworkStartup.h"
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"

void AppStoreActivity::stopRefresh() {
  resumeIcons_ = resumeIcons_ || (catalog_.refreshing() && catalog_.missingIcons() > 0);
  connection_ = Connection::Idle;
  catalog_.cancelAndJoin();
}

void AppStoreActivity::refresh(bool chooseNetwork) {
  if (catalog_.refreshing()) return;
  if (WiFi.status() == WL_CONNECTED) {
    connection_ = Connection::Idle;
    catalog_.startRefresh();
    RenderLock lock;
    if (updateStatus()) requestUpdate();
    return;
  }
  if (!chooseNetwork) {
    connection_ = Connection::Pending;
    return;
  }
  stopRefresh();
  wifiStarted_ = wifiStarted_ || WiFi.getMode() == WIFI_OFF;
  startActivityForResultWith<WifiSelectionActivity>([this](const ActivityResult& result) {
    guardHeldButtons();
    if (!result.isCancelled) catalog_.startRefresh();
    browser(true);
  });
}

void AppStoreActivity::pumpRefresh() {
  catalog_.poll();
  // Cached content must be on screen before any network setup can reclaim
  // fonts or start allocating transport buffers.
  if (!routingReady()) return;
  bool connectionChanged = false;
  switch (connection_) {
    case Connection::Pending:
      connectionChanged = true;
      connection_ = Connection::Idle;
      if (WiFi.status() == WL_CONNECTED) {
        catalog_.startRefresh();
      } else if (WiFi.getMode() == WIFI_OFF) {
        WIFI_STORE.loadFromFile();
        const auto last = WIFI_STORE.getLastConnectedSsid();
        const auto credential = last.empty() ? WIFI_STORE.getCredentialAt(0) : WIFI_STORE.findCredential(last);
        if (credential && NetworkStartup::setMode(renderer, WIFI_STA)) {
          wifiStarted_ = true;
          WiFi.persistent(false);
          WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
          connection_ = Connection::Connecting;
          connectionAt_ = millis();
        } else {
          // Retain the cached grid. Network selection is an explicit action,
          // so an unavailable saved AP never replaces the storefront.
          catalog_.startRefresh();
        }
      } else {
        // Another service may own this station/channel (notably ESP-NOW).
        // Observe its connection without disconnecting or changing channel.
        connection_ = Connection::Connecting;
        connectionAt_ = millis();
      }
      break;
    case Connection::Connecting:
      if (WiFi.status() == WL_CONNECTED || millis() - connectionAt_ >= 12000) {
        connection_ = Connection::Idle;
        connectionChanged = true;
        catalog_.startRefresh();
      }
      break;
    case Connection::Idle:
      break;
  }
  if (view_ != View::Browser) return;
  if (resumeIcons_ && connection_ == Connection::Idle && !catalog_.refreshing() && WiFi.status() == WL_CONNECTED) {
    // Resume once after a child/install interrupted icon downloads. A failed
    // retry stays visible and waits for the user's refresh; no network loop.
    resumeIcons_ = false;
    if (catalog_.missingIcons() > 0) catalog_.startRefresh();
  }
  if (revision_ != catalog_.revision()) {
    // Keep the old hit table/data together until the complete gesture ends.
    // A held tap must never be released onto a different, freshly sorted app.
    if (!inputActive()) {
      browser(true);
      return;
    }
  }
  const bool iconsChanged = iconRevision_ != catalog_.iconRevision();
  const bool statusChanged = lastStatus_ != catalog_.status();
  const bool checkIcons = iconsChanged && (!catalog_.refreshing() || millis() - lastIconPaint_ >= 2000);
  if (!checkIcons && !statusChanged && !connectionChanged) return;
  RenderLock lock;
  const bool imageChanged = checkIcons && refreshIcons();
  const bool textChanged = updateStatus();
  if (imageChanged || textChanged) requestUpdate();
}

bool AppStoreActivity::updateStatus() {
  using Status = crossstore::StoreCatalog::Status;
  const char* text = "";
  lastStatus_ = catalog_.status();
  if (connection_ != Connection::Idle) {
    // Cache-first, Wi-Fi association and HTTP refresh are one visible job.
    // Avoid a separate e-ink refresh for each transient connection stage.
    text = tr(STR_STORE_REFRESHING);
  } else {
    switch (lastStatus_) {
      case Status::Empty:
        text = tr(STR_STORE_CONNECT_CATALOG);
        break;
      case Status::Cached:
        text = tr(STR_STORE_CACHED);
        break;
      case Status::Refreshing:
        text = tr(STR_STORE_REFRESHING);
        break;
      case Status::Ready:
        text = catalog_.missingIcons() > 0 ? tr(STR_STORE_ICONS_PENDING) : tr(STR_STORE_CURRENT);
        break;
      case Status::Failed:
        text = catalog_.hasCache() ? tr(STR_STORE_CACHE_RETAINED) : tr(STR_STORE_CONNECT_CATALOG);
        break;
    }
  }
  bool changed = std::strcmp(statusText_, text) != 0;
  if (changed) std::snprintf(statusText_, sizeof(statusText_), "%s", text);
  const size_t offset = offsets_[static_cast<unsigned>(tab_)];
  const size_t pages = std::max<size_t>(1, (total_ + pageSize_ - 1) / pageSize_);
  char page[sizeof(pageText_)];
  std::snprintf(page, sizeof(page), tr(STR_STORE_GRID_PAGE), static_cast<unsigned>(offset / pageSize_ + 1),
                static_cast<unsigned>(pages), static_cast<unsigned>(total_));
  if (std::strcmp(pageText_, page)) {
    std::strcpy(pageText_, page);
    changed = true;
  }
  return changed;
}
