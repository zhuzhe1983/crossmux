#include "Uninstall.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <iterator>

#include "ChatBuildConfig.h"
#include "Package.h"
#include "Permissions.h"
#if CROSSMUX_CHAT
#include "activities/apps/chat/ChatService.h"
#endif

namespace crossstore {
namespace {
bool removeFile(const char* path) {
  if (!Storage.exists(path)) return true;
  {
    auto file = Storage.open(path);
    if (!file || file.isDirectory()) return false;
  }
  return Storage.remove(path);
}

bool removeSidecars(const char* path) {
  char sibling[96];
  for (const auto* suffix : {".previous", ".part"}) {
    if (std::snprintf(sibling, sizeof(sibling), "%s%s", path, suffix) >= static_cast<int>(sizeof(sibling)) ||
        !removeFile(sibling))
      return false;
  }
  return true;
}

bool removePrivateDirectory(const char* root) {
  if (!Storage.exists(root)) return true;
  // No recursive stack or full directory list: reopen only the next leaf.
  // The application API creates at most 32 flat keys; bounds also cover the
  // fixed chat files and refuse arbitrarily deep/large card-side additions.
  char path[128];
  const int rootSize = std::snprintf(path, sizeof(path), "%s", root);
  if (rootSize <= 0 || rootSize >= static_cast<int>(sizeof(path))) return false;
  uint8_t lengths[8]{static_cast<uint8_t>(rootSize)};
  unsigned depth = 0;
  for (unsigned step = 0; step < 512; ++step) {
    bool childFound = false, childDirectory = false;
    {
      auto directory = Storage.open(path);
      if (!directory || !directory.isDirectory()) return false;
      auto child = directory.openNextFile();
      if (child) {
        char name[64]{};
        if (!child.getName(name, sizeof(name))) return false;
        const size_t length = std::strlen(name);
        if (!length || length >= sizeof(name) - 1 || !std::strcmp(name, ".") || !std::strcmp(name, "..") ||
            std::strpbrk(name, "/\\:") || lengths[depth] + length + 1 >= sizeof(path))
          return false;
        path[lengths[depth]] = '/';
        std::memcpy(path + lengths[depth] + 1, name, length + 1);
        childFound = true;
        childDirectory = child.isDirectory();
      }
    }  // Close every handle before deleting or descending into a child.
    if (!childFound) {
      if (!Storage.rmdir(path)) return false;
      if (!depth) return true;
      path[lengths[--depth]] = 0;
    } else if (childDirectory) {
      if (depth + 1 == std::size(lengths)) return false;
      lengths[++depth] = static_cast<uint8_t>(std::strlen(path));
    } else {
      if (!Storage.remove(path)) return false;
      path[lengths[depth]] = 0;
    }
  }
  LOG_ERR("STORE", "Private data deletion limit reached");
  return false;
}

const char* chatDirectory(const char* id) {
  if (!std::strcmp(id, "rf_chat")) return "/.crosspoint/rf_chat";
  if (!std::strcmp(id, "wifi_chat")) return "/.crosspoint/wifi_chat";
  if (!std::strcmp(id, "esp_chat")) return "/.crosspoint/esp_chat";
  return nullptr;
}

#if CROSSMUX_CHAT
bool chatKind(const char* id, chat::Kind& kind) {
  if (!std::strcmp(id, "rf_chat"))
    kind = chat::Kind::Radio;
  else if (!std::strcmp(id, "wifi_chat"))
    kind = chat::Kind::Wifi;
  else if (!std::strcmp(id, "esp_chat"))
    kind = chat::Kind::EspNow;
  else
    return false;
  return true;
}
#endif
}  // namespace

bool uninstallPackage(const char* id, bool includeData) {
  if (!validId(id) || !Storage.ready()) return false;
#if CROSSMUX_CHAT
  chat::Kind kind = chat::Kind::Radio;
  const bool chatApp = chatKind(id, kind);
  if (chatApp && !chat::service::canUninstall(kind)) return false;
#endif
  if (!setGrantedPermissions(id, 0)) return false;
#if CROSSMUX_CHAT
  if (chatApp && !chat::service::stopForUninstall(kind)) return false;
#endif
  char path[80];
  std::snprintf(path, sizeof(path), "%s/%s.csp", INSTALL_DIR, id);
  // Recovery siblings must disappear first, otherwise the next launcher can
  // restore an uninstalled package. A failure keeps the current CSP intact.
  if (!removeSidecars(path)) return false;
  if (includeData) {
    std::snprintf(path, sizeof(path), "/.crosspoint/appdata/%s", id);
    if (!removePrivateDirectory(path)) return false;
    if (const auto* chatPath = chatDirectory(id); chatPath && !removePrivateDirectory(chatPath)) return false;
    std::snprintf(path, sizeof(path), "/.crosspoint/app-permissions/%s", id);
    if (!removeSidecars(path) || !removeFile(path)) return false;
  }
  std::snprintf(path, sizeof(path), "%s/%s.csp", INSTALL_DIR, id);
  return removeFile(path);
}
}  // namespace crossstore
