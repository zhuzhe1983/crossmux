#include "AppStoreActivity.h"

#include <CrossStoreRuntime.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ChatBuildConfig.h"
#include "LuaAppActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "crossstore/Permissions.h"
#include "crossstore/RecoverableFile.h"
#include "network/HttpDownloader.h"
#if CROSSMUX_CHAT
#include "activities/apps/chat/ChatService.h"
#endif

namespace {
constexpr char SOURCE_FILE[] = "/.crosspoint/crossstore-url.txt";
constexpr char DOWNLOAD_FILE[] = "/.crosspoint/apps/download.part";
bool httpUrl(const char* text) {
  const size_t size = std::strlen(text);
  if (size < 10 || size >= 256 || (std::strncmp(text, "http://", 7) && std::strncmp(text, "https://", 8))) return false;
  for (size_t i = 0; i < size; ++i)
    if (static_cast<unsigned char>(text[i]) <= 32 || text[i] == '\\') return false;
  return !std::strchr(text, '@');
}
void requirementText(const crossstore::Package& package, char* out, size_t capacity) {
  out[0] = 0;
  for (const auto& capability : crossstore::CAPABILITIES) {
    if (!(package.requiredCapabilities & capability.bit)) continue;
    const size_t used = std::strlen(out);
    std::snprintf(out + used, capacity - used, "%s%s", used ? ", " : "", capability.name);
  }
  if (!*out) std::snprintf(out, capacity, "%s", tr(STR_STORE_NO_SPECIAL_HARDWARE));
}
}  // namespace

void AppStoreActivity::onEnter() {
  Activity::onEnter();
  guardHeldButtons();
  resetUi();
  app.on(ACTION_ROW, &AppStoreActivity::rowCallback, this);
  app.on(ACTION_APP, &AppStoreActivity::appCallback, this);
  app.on(ACTION_CONTROL, &AppStoreActivity::controlCallback, this);
  app.on(ACTION_DETAIL, &AppStoreActivity::detailCallback, this);
  app.on(ACTION_CONFIRM, &AppStoreActivity::confirmationCallback, this);
  app.setScreen(&AppStoreActivity::screenCallback, this);
  crossstore::recoverInstalls();
  crossstore::recoverFile(Storage, SOURCE_FILE);
  std::snprintf(source_, sizeof(source_), "%s", crossstore::DEFAULT_INDEX);
  {
    auto file = Storage.open(SOURCE_FILE);
    if (file && file.size() < sizeof(source_)) {
      char saved[256]{};
      const size_t size = file.size();
      if (file.read(saved, size) == static_cast<int>(size) && httpUrl(saved)) std::strcpy(source_, saved);
    }
  }
  catalog_.configure(source_);
  reloadInstalled();
  // Network work starts only after the first cached frame has been published.
  connection_ = Connection::Pending;
  browser();
}
void AppStoreActivity::onExit() {
  stopRefresh();
  icons_.clear();
  Storage.remove(DOWNLOAD_FILE);
  if (wifiStarted_) {
#if CROSSMUX_CHAT
    if (chat::service::background(chat::Kind::Wifi) || chat::service::background(chat::Kind::EspNow)) {
      chat::service::ownWifi();
    } else
#endif
    {
      WiFi.disconnect(false);
      WiFi.mode(WIFI_OFF);
    }
  }
  Activity::onExit();
}
void AppStoreActivity::resetRows(View view) {
  closeRouting();
  app.clearTapFlash();
  view_ = view;
  rowCount_ = 0;
  nav_.reset();
}
void AppStoreActivity::addRow(const char* label, const char* subtitle) {
  if (rowCount_ == std::size(rows_)) return;
  std::snprintf(labels_[rowCount_], sizeof(labels_[0]), "%s", label);
  std::snprintf(subtitles_[rowCount_], sizeof(subtitles_[0]), "%s", subtitle);
  rows_[rowCount_] = {};
  rows_[rowCount_].label = labels_[rowCount_];
  rows_[rowCount_].subtitle = subtitles_[rowCount_];
  rows_[rowCount_].actionValue = static_cast<int16_t>(rowCount_);
  ++rowCount_;
}
void AppStoreActivity::reloadInstalled() {
  RenderLock lock;
  installedCount_ = 0;
  auto directory = Storage.open(crossstore::INSTALL_DIR);
  if (!directory || !directory.isDirectory()) return;
  while (auto file = directory.openNextFile()) {
    char filename[64]{};
    file.getName(filename, sizeof(filename));
    if (!crossstore::installedFilename(filename)) continue;
    const std::string path = std::string(crossstore::INSTALL_DIR) + "/" + filename;
    if (!crossstore::readPackage(path, installedPackage_) || path != crossstore::installedPath(installedPackage_.id))
      continue;
    if (installedCount_ == std::size(installed_)) break;
    auto& item = installed_[installedCount_++];
    std::snprintf(item.id, sizeof(item.id), "%s", installedPackage_.id);
    std::snprintf(item.version, sizeof(item.version), "%s", installedPackage_.version);
    std::snprintf(item.name, sizeof(item.name), "%s", installedPackage_.name);
  }
  std::sort(std::begin(installed_), std::begin(installed_) + installedCount_,
            [](const auto& a, const auto& b) { return std::strcmp(a.name, b.name) < 0; });
}
int AppStoreActivity::installedIndex(const char* id) const {
  for (size_t i = 0; i < installedCount_; ++i)
    if (!std::strcmp(installed_[i].id, id)) return static_cast<int>(i);
  return -1;
}
void AppStoreActivity::browser(bool preserveSelection) {
  RenderLock lock;
  HeaderBackTapTarget::clear();
  // Sample before reading; a commit during the read remains detectable next loop.
  revision_ = catalog_.revision();
  char selected[32]{};
  if (preserveSelection && view_ == View::Browser && focus_ >= 3 && static_cast<size_t>(focus_ - 3) < count_)
    std::strcpy(selected, entries_[focus_ - 3].package.id);
  else if (preserveSelection && view_ == View::Details && chosen_ < count_)
    std::strcpy(selected, entries_[chosen_].package.id);
  closeRouting();
  app.clearTapFlash();
  view_ = View::Browser;
  pageSize_ = geometry().capacity;
  auto& offset = offsets_[static_cast<unsigned>(tab_)];
  total_ = tab_ == Tab::All ? catalog_.total() : installedCount_;
  if (*selected) {
    size_t index = 0;
    bool found = false;
    if (tab_ == Tab::All)
      found = catalog_.lookup(selected, lookupEntry_, &index);
    else {
      const int local = installedIndex(selected);
      found = local >= 0;
      index = found ? static_cast<size_t>(local) : 0;
    }
    if (found) {
      offset = index / pageSize_ * pageSize_;
      focus_ = static_cast<int>(index % pageSize_) + 3;
    }
  }
  offset = total_ ? std::min(offset / pageSize_ * pageSize_, (total_ - 1) / pageSize_ * pageSize_) : 0;
  count_ = 0;
  switch (tab_) {
    case Tab::All:
      catalog_.load(offset, pageSize_, entries_, count_, total_);
      break;
    case Tab::Installed:
      for (size_t i = offset; i < installedCount_ && count_ < pageSize_; ++i) {
        auto& entry = entries_[count_];
        entry = {};
        if (!catalog_.lookup(installed_[i].id, entry) &&
            !crossstore::readPackage(crossstore::installedPath(installed_[i].id), entry.package))
          continue;
        ++count_;
      }
      break;
  }
  for (size_t i = 0; i < count_; ++i) {
    const int local = installedIndex(entries_[i].package.id);
    marks_[i] = crossstore::installMark(local >= 0 ? installed_[local].version : nullptr, entries_[i].package.version);
  }
  if (focus_ >= 3) focus_ = count_ ? std::clamp(focus_, 3, static_cast<int>(count_) + 2) : 0;
  loadIcons();
  updateStatus();
  requestUpdate();
}
void AppStoreActivity::loadIcons() {
  icons_.clear();
  catalogIconSlots_ = loadedIconSlots_ = 0;
  refreshIcons();
}
bool AppStoreActivity::refreshIcons() {
  // Sample before reading so a newly published image remains detectable.
  iconRevision_ = catalog_.iconRevision();
  bool changed = false;
  const int levels = renderer.getGrayscaleLevels();
  const auto layout = geometry();
  const int lineHeight = renderer.getLineHeight(uiScaleSpec(false).bodyFontId);
  for (size_t i = 0; i < count_; ++i) {
    const auto bit = static_cast<uint16_t>(1u << i);
    if (catalogIconSlots_ & bit) continue;
    const bool wasLoaded = (loadedIconSlots_ & bit) != 0;
    char path[256];
    const int size = crossstore::StoreGrid::iconSize(layout.cell(i), lineHeight, levels);
    if (catalog_.iconPath(entries_[i], path, sizeof(path))) {
      if (icons_.loadBundle(i, path, levels, size)) {
        catalogIconSlots_ |= bit;
        loadedIconSlots_ |= bit;
        changed = true;
        continue;
      }
      // A failed bundle read clears the slot. Restore its installed fallback,
      // and repaint a placeholder if that restoration also fails.
      loadedIconSlots_ &= static_cast<uint16_t>(~bit);
    }
    if (!(loadedIconSlots_ & bit) && installedIndex(entries_[i].package.id) >= 0 &&
        icons_.loadPackage(i, crossstore::installedPath(entries_[i].package.id).c_str(), levels, size))
      loadedIconSlots_ |= bit;
    changed = changed || wasLoaded != ((loadedIconSlots_ & bit) != 0);
  }
  lastIconPaint_ = millis();
  return changed;
}
void AppStoreActivity::switchTab(Tab tab) {
  if (tab == tab_ && view_ == View::Browser) return;
  {
    RenderLock lock;
    closeRouting();
    tab_ = tab;
    focus_ = static_cast<int>(tab);
  }
  browser();
}
void AppStoreActivity::turnPage(int direction) {
  {
    RenderLock lock;
    auto& offset = offsets_[static_cast<unsigned>(tab_)];
    if (direction < 0) {
      if (!offset) return;
      offset = offset > pageSize_ ? offset - pageSize_ : 0;
    } else {
      if (offset + count_ >= total_) return;
      offset += pageSize_;
    }
    focus_ = 3;
  }
  browser();
}
void AppStoreActivity::settings() {
  RenderLock lock;
  resetRows(View::Settings);
  addRow(tr(STR_STORE_PERMISSIONS));
  addRow(tr(STR_STORE_SOURCE), source_);
  addRow(tr(STR_STORE_DEVICE_READONLY));
  addRow(tr(STR_STORE_REFRESH));
  requestUpdate();
}
void AppStoreActivity::permissions(size_t index) {
  if (index >= count_) return;
  RenderLock lock;
  chosen_ = index;
  resetRows(View::PermissionDetails);
  const auto& package = entries_[index].package;
  const auto granted = crossstore::grantedPermissions(package.id);
  for (const auto& permission : crossstore::PERMISSIONS) {
    if (!(package.permissions & permission.bit)) continue;
    addRow(I18N.get(permission.title), (granted & permission.bit) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
  }
  if (!rowCount_) addRow(tr(STR_STORE_PERMISSION_NONE));
  requestUpdate();
}
void AppStoreActivity::fail(const char* detail) {
  RenderLock lock;
  resetRows(View::Error);
  std::snprintf(error_, sizeof(error_), "%s", detail);
  addRow(tr(STR_STORE_FAILED), error_);
  requestUpdate();
}
void AppStoreActivity::permissionApps() {
  RenderLock lock;
  resetRows(View::Permissions);
  count_ = 0;
  morePermissions_ = permissionOffset_ + PAGE_SIZE < installedCount_;
  for (size_t i = permissionOffset_; i < installedCount_ && count_ < PAGE_SIZE; ++i) {
    auto& entry = entries_[count_];
    entry = {};
    if (!crossstore::readPackage(crossstore::installedPath(installed_[i].id), entry.package)) continue;
    addRow(entry.package.name, entry.package.version);
    ++count_;
  }
  if (morePermissions_) addRow(tr(STR_STORE_NEXT));
  if (permissionOffset_) addRow(tr(STR_STORE_PREVIOUS));
  if (!rowCount_) addRow(tr(STR_STORE_EMPTY));
  requestUpdate();
}
void AppStoreActivity::details(size_t index) {
  if (index >= count_) return;
  RenderLock lock;
  chosen_ = index;
  resetRows(View::Details);
  const auto& entry = entries_[index];
  addRow(entry.package.name, entry.package.version);
  addRow(tr(STR_STORE_DESCRIPTION), entry.package.description);
  char text[160];
  requirementText(entry.package, text, sizeof(text));
  addRow(tr(STR_STORE_REQUIRES), text);
  const bool allowed = crossstore::compatible(entry.package, text, sizeof(text));
  addRow(allowed                                       ? tr(STR_STORE_COMPATIBLE)
         : entry.package.api > crossstore::API_VERSION ? tr(STR_STORE_FIRMWARE_UPDATE_REQUIRED)
                                                       : tr(STR_STORE_MISSING),
         allowed ? "" : text);
  text[0] = 0;
  for (const auto& permission : crossstore::PERMISSIONS) {
    if (!(entry.package.permissions & permission.bit)) continue;
    const size_t used = std::strlen(text);
    std::snprintf(text + used, sizeof(text) - used, "%s%s", used ? ", " : "", I18N.get(permission.title));
  }
  addRow(tr(STR_STORE_PERMISSIONS), *text ? text : tr(STR_STORE_PERMISSION_NONE));
  openRow_ = installRow_ = removeRow_ = primaryRow_ = -1;
  const bool exists = Storage.exists(crossstore::installedPath(entry.package.id).c_str());
  const bool canOpen = exists &&
                       crossstore::readPackage(crossstore::installedPath(entry.package.id), installedPackage_) &&
                       !std::strcmp(installedPackage_.id, entry.package.id) &&
                       crossstore::compatible(installedPackage_, text, sizeof(text));
  const int local = installedIndex(entry.package.id);
  const bool newer = local < 0 || crossstore::compareVersions(entry.package.version, installed_[local].version) > 0;
  const bool canInstall = *entry.artifact && allowed && (!canOpen || newer);
  // Information can be selected/read with keys, but never activates an action.
  for (size_t row = 0; row < rowCount_; ++row) rows_[row].enabled = false;
  if (canOpen && canInstall) {
    openRow_ = static_cast<int>(rowCount_);
    addRow(tr(STR_STORE_OPEN));
  }
  if (exists) {
    removeRow_ = static_cast<int>(rowCount_);
    addRow(tr(STR_STORE_REMOVE));
  }
  // The last row only stores the fixed button's label/action. It is excluded
  // from the scroll list, so the main action is visible once at every offset.
  if (canInstall) {
    primaryRow_ = installRow_ = static_cast<int>(rowCount_);
    addRow(exists ? tr(STR_STORE_UPDATE) : tr(STR_STORE_INSTALL));
  } else if (canOpen) {
    primaryRow_ = openRow_ = static_cast<int>(rowCount_);
    addRow(tr(STR_STORE_OPEN));
  }
  primaryFocused_ = primaryRow_ >= 0;
  if (primaryFocused_) {
    nav_.selected = -1;
    nav_.followOnBuild = false;
  }
  requestUpdate();
}
bool AppStoreActivity::fetch(const char* url, const char* destination, size_t maximum) {
  if (!httpUrl(url)) return false;
  {
    RenderLock lock;
    resetRows(View::Busy);
    addRow(tr(STR_DOWNLOADING));
  }
  requestUpdateAndWait();
  HalPowerManager::Lock power;
  bool result = false;
  {
    auto file = Storage.open(destination, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) return false;
    size_t total = 0;
    result = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, size_t bytes) {
      mappedInput.update(true);
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) return false;
      if (bytes > maximum - total || file.write(data, bytes) != bytes) return false;
      total += bytes;
      return true;
    });
    file.flush();
  }
  if (!result) Storage.remove(destination);
  return result;
}
void AppStoreActivity::install() {
  stopRefresh();
  const auto& entry = entries_[chosen_];
  char missing[160];
  if (!crossstore::hasInstallRoom(entry.package.id)) {
    fail(tr(STR_STORE_APP_LIMIT));
    return;
  }
  if (!crossstore::compatible(entry.package, missing, sizeof(missing))) {
    fail(missing);
    return;
  }
  if (!httpUrl(entry.artifact) ||
      (!Storage.exists(crossstore::INSTALL_DIR) && !Storage.mkdir(crossstore::INSTALL_DIR)) ||
      !fetch(entry.artifact, DOWNLOAD_FILE, entry.bytes)) {
    fail(tr(STR_DOWNLOAD_FAILED));
    return;
  }
  crossstore::Package downloaded;
  if (!crossstore::verifyHash(DOWNLOAD_FILE, entry.bytes, entry.sha256) ||
      !crossstore::readPackage(DOWNLOAD_FILE, downloaded) || std::strcmp(downloaded.id, entry.package.id) ||
      std::strcmp(downloaded.version, entry.package.version) || downloaded.api != entry.package.api ||
      downloaded.requiredCapabilities != entry.package.requiredCapabilities ||
      downloaded.memoryBytes != entry.package.memoryBytes || downloaded.permissions != entry.package.permissions ||
      !crossstore::compatible(downloaded, missing, sizeof(missing))) {
    Storage.remove(DOWNLOAD_FILE);
    fail(tr(STR_STORE_INVALID_PACKAGE));
    return;
  }
  size_t scriptSize = 0;
  auto script = crossstore::readEntry(DOWNLOAD_FILE, "main.lua", crossstore::MAX_SCRIPT_BYTES, scriptSize);
  if (!script || script.get()[0] == 0x1b || std::memchr(script.get(), 0, scriptSize)) {
    Storage.remove(DOWNLOAD_FILE);
    fail(tr(STR_STORE_INVALID_PACKAGE));
    return;
  }
  script.reset();
  if (!crossstore::commitFile(Storage, DOWNLOAD_FILE, crossstore::installedPath(downloaded.id))) {
    fail(tr(STR_STORE_FAILED));
    return;
  }
  if (!crossstore::setGrantedPermissions(downloaded.id, downloaded.permissions)) {
    fail(tr(STR_STORE_FAILED));
    return;
  }
  reloadInstalled();
  details(chosen_);
}
void AppStoreActivity::changeSource() {
  stopRefresh();
  startActivityForResultWith<KeyboardEntryActivity>(
      [this](const ActivityResult& result) {
        guardHeldButtons();
        if (result.isCancelled) return;
        const auto& url = std::get<KeyboardResult>(result.data).text;
        if (!httpUrl(url.c_str())) {
          fail(tr(STR_STORE_BAD_SOURCE));
          return;
        }
        {
          auto file = Storage.open("/.crosspoint/crossstore-url.part", O_WRONLY | O_CREAT | O_TRUNC);
          if (!file || file.write(url.data(), url.size()) != url.size()) {
            fail(tr(STR_STORE_FAILED));
            return;
          }
          file.flush();
        }
        if (!crossstore::commitFile(Storage, "/.crosspoint/crossstore-url.part", SOURCE_FILE)) {
          fail(tr(STR_STORE_FAILED));
          return;
        }
        std::strcpy(source_, url.c_str());
        catalog_.configure(source_);
        offsets_[0] = offsets_[1] = 0;
        connection_ = Connection::Pending;
        settings();
      },
      tr(STR_STORE_SOURCE), source_, 255);
}
void AppStoreActivity::capabilities() {
  RenderLock lock;
  resetRows(View::Capabilities);
  const auto capabilities = crossstore::deviceCapabilities();
  struct CapabilityTitle {
    const char* name;
    StrId title;
  };
  constexpr CapabilityTitle titles[] = {
      {"buzzer", StrId::STR_CAPABILITY_BUZZER},
      {"audio", StrId::STR_CAPABILITY_AUDIO},
      {"wifi", StrId::STR_CAPABILITY_WIFI},
      {"lora", StrId::STR_CAPABILITY_LORA},
      {"esp_now", StrId::STR_CAPABILITY_ESP_NOW},
      {"tox", StrId::STR_CAPABILITY_TOX},
      {"imu", StrId::STR_CAPABILITY_IMU},
      {"touch", StrId::STR_CAPABILITY_TOUCH},
      {"frontlight", StrId::STR_CAPABILITY_FRONTLIGHT},
      {"rtc", StrId::STR_CAPABILITY_RTC},
      {"psram", StrId::STR_CAPABILITY_PSRAM},
      {"storage", StrId::STR_CAPABILITY_STORAGE},
  };
  for (const auto& item : titles)
    addRow(I18N.get(item.title), (capabilities & crossstore::capabilityBit(item.name))
                                     ? tr(STR_CAPABILITY_SUPPORTED)
                                     : tr(STR_CAPABILITY_UNSUPPORTED));
  addRow(tr(STR_CAPABILITY_MICROPHONE),
         HalSystem::hasConfiguredMicrophone() ? tr(STR_MIC_PRESENT_NO_APP_CAPTURE) : tr(STR_HARDWARE_NOT_CONFIGURED));
  requestUpdate();
}
void AppStoreActivity::activateIndex(int index) {
  if (index < 0 || static_cast<size_t>(index) >= rowCount_) return;
  app.clearTapFlash();
  switch (view_) {
    case View::Settings:
      switch (index) {
        case 0:
          permissionOffset_ = 0;
          permissionApps();
          break;
        case 1:
          changeSource();
          break;
        case 2:
          capabilities();
          break;
        case 3:
          browser();
          refresh(true);
          break;
      }
      break;
    case View::PermissionDetails: {
      const auto& package = entries_[chosen_].package;
      int row = 0;
      for (const auto& permission : crossstore::PERMISSIONS) {
        if (!(package.permissions & permission.bit)) continue;
        if (row++ != index) continue;
        if (!crossstore::setGrantedPermissions(package.id, crossstore::grantedPermissions(package.id) ^ permission.bit))
          fail(tr(STR_STORE_FAILED));
        else
          permissions(chosen_);
        break;
      }
      break;
    }
    case View::Permissions:
      if (static_cast<size_t>(index) < count_)
        permissions(index);
      else if (morePermissions_ && static_cast<size_t>(index) == count_) {
        permissionOffset_ += PAGE_SIZE;
        permissionApps();
      } else if (permissionOffset_) {
        permissionOffset_ -= PAGE_SIZE;
        permissionApps();
      }
      break;
    case View::Details:
      if (index == openRow_) {
        stopRefresh();
        {
          RenderLock lock;
          icons_.clear();
        }
        startActivityForResultWith<LuaAppActivity>(
            [this](const ActivityResult&) {
              guardHeldButtons();
              reloadInstalled();
              details(chosen_);
            },
            entries_[chosen_].package.id);
      } else if (index == installRow_) {
        showInstallConfirmation();
      } else if (index == removeRow_) {
        showRemoveConfirmation();
      }
      break;
    case View::Error:
      browser();
      break;
    case View::ConfirmInstall:
    case View::ConfirmRemove:
    case View::Browser:
    case View::Capabilities:
    case View::Busy:
      break;
  }
}
void AppStoreActivity::onBackButton() {
  switch (view_) {
    case View::ConfirmInstall:
    case View::ConfirmRemove:
      details(chosen_);
      break;
    case View::Browser:
      stopRefresh();
      finish();
      break;
    case View::Details:
      browser(true);
      break;
    case View::PermissionDetails:
      permissionApps();
      break;
    case View::Permissions:
    case View::Capabilities:
      settings();
      break;
    case View::Settings:
    case View::Busy:
    case View::Error:
      browser();
      break;
  }
}
