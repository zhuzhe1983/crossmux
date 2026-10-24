#include "Permissions.h"

#include <HalStorage.h>

#include <cstdio>
#include <cstring>

#include "ChatBuildConfig.h"
#include "Package.h"
#include "RecoverableFile.h"
#if CROSSMUX_CHAT
#include "activities/apps/chat/ChatService.h"
#endif

namespace crossstore {
uint32_t permissionBit(const char* name) {
  for (const auto& permission : PERMISSIONS)
    if (!std::strcmp(permission.name, name)) return permission.bit;
  return 0;
}
uint32_t grantedPermissions(const char* id) {
  if (!validId(id)) return 0;
  char path[96];
  std::snprintf(path, sizeof(path), "/.crosspoint/app-permissions/%s", id);
  if (!recoverFile(Storage, path)) return 0;
  auto file = Storage.open(path);
  uint32_t granted = 0;
  if (!file || file.size() != sizeof(granted) || file.read(&granted, sizeof(granted)) != sizeof(granted)) return 0;
  return granted;
}
bool setGrantedPermissions(const char* id, uint32_t granted) {
  if (!validId(id)) return false;
  constexpr char dir[] = "/.crosspoint/app-permissions";
  if (!Storage.exists(dir) && !Storage.mkdir(dir)) return false;
  char path[96], temp[104];
  std::snprintf(path, sizeof(path), "%s/%s", dir, id);
  std::snprintf(temp, sizeof(temp), "%s.part", path);
  {
    auto file = Storage.open(temp, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file || file.write(reinterpret_cast<const uint8_t*>(&granted), sizeof(granted)) != sizeof(granted))
      return false;
    file.flush();
  }
  if (!commitFile(Storage, temp, path)) return false;
#if CROSSMUX_CHAT
  // Revocation takes effect immediately, including an awake background listener.
  struct ChatApp {
    const char* id;
    chat::Kind kind;
    const char* transport;
  };
  constexpr ChatApp apps[] = {{"rf_chat", chat::Kind::Radio, "radio"},
                              {"wifi_chat", chat::Kind::Wifi, "network"},
                              {"esp_chat", chat::Kind::EspNow, "radio"}};
  for (const auto& app : apps) {
    if (std::strcmp(app.id, id)) continue;
    if (!(granted & permissionBit("background")) || !(granted & permissionBit(app.transport)))
      chat::service::setBackground(app.kind, false);
  }
#endif
  return true;
}
bool permissionGranted(const char* id, const char* permission) {
  return (grantedPermissions(id) & permissionBit(permission)) != 0;
}
}  // namespace crossstore
