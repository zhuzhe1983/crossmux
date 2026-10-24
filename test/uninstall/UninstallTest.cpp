#include <HalStorage.h>
#include <freertos/task.h>

#include <cassert>
#include <iostream>

#include "ChatBuildConfig.h"
#include "crossstore/Package.h"
#include "crossstore/Permissions.h"
#include "crossstore/Uninstall.h"
#if CROSSMUX_CHAT
#include <UninstallTestSession.h>

#include "activities/apps/chat/ChatService.h"
#endif

// Only this unrelated ZIP/path helper is supplied by the harness. Validation,
// permission persistence, ownership, worker release, and deletion are real.
namespace crossstore {
std::string installedPath(const char* id) { return std::string(INSTALL_DIR) + "/" + id + ".csp"; }
}  // namespace crossstore

namespace {
std::string package(const std::string& id) { return "/.crosspoint/apps/" + id + ".csp"; }
std::string data(const std::string& id) { return "/.crosspoint/appdata/" + id; }
std::string permission(const std::string& id) { return "/.crosspoint/app-permissions/" + id; }
void put(const std::string& path, const std::string& value = "keep") {
  std::filesystem::create_directories(Storage.path(path.c_str()).parent_path());
  std::ofstream file(Storage.path(path.c_str()), std::ios::binary);
  file << value;
  assert(file.good());
}
bool exists(const std::string& path) { return Storage.exists(path.c_str()); }
void fixtures(const std::string& id) {
  put(package(id));
  put(package(id) + ".previous");
  put(package(id) + ".part");
  put(data(id) + "/state");
  put(data(id) + "/state.previous");
  put(data(id) + "/state.part");
  put(data(id) + "/nested/settings");
  put("/CrossStore/" + id + "/export.txt");
  put("/.crosspoint/rf_chat/messages-v1.txt");
  put("/.crosspoint/wifi_chat/identity.tox");
  put("/.crosspoint/wifi_chat/identity.tox.previous");
  put("/.crosspoint/esp_chat/messages-v2.txt");
  put("/.crosspoint/weread/token");
  put("/.crosspoint/reading/progress");
  put("/book/example.epub");
  put(package("another_app"));
  put(data("another_app") + "/state");
  assert(crossstore::setGrantedPermissions(id.c_str(), 0x3ff));
  assert(crossstore::setGrantedPermissions("another_app", 0x3ff));
  storageEvents.clear();
}
void retained(const std::string& id) {
  assert(exists("/CrossStore/" + id + "/export.txt"));
  assert(exists("/.crosspoint/weread/token"));
  assert(exists("/.crosspoint/reading/progress"));
  assert(exists("/book/example.epub"));
  assert(exists(package("another_app")));
  assert(exists(data("another_app") + "/state"));
  assert(crossstore::grantedPermissions("another_app") == 0x3ff);
  if (id != "rf_chat") assert(exists("/.crosspoint/rf_chat/messages-v1.txt"));
  if (id != "wifi_chat") assert(exists("/.crosspoint/wifi_chat/identity.tox"));
  if (id != "esp_chat") assert(exists("/.crosspoint/esp_chat/messages-v2.txt"));
}
void removed(const std::string& id, bool all) {
  assert(!exists(package(id)));
  assert(!exists(package(id) + ".previous"));
  assert(!exists(package(id) + ".part"));
  assert(exists(data(id)) != all);
  assert(exists(permission(id)) != all);
  assert(crossstore::grantedPermissions(id.c_str()) == 0);
  assert(!exists(permission(id) + ".previous"));
  assert(!exists(permission(id) + ".part"));
  assert(storageEvents.back() == "remove:" + package(id));
  retained(id);
}
#if CROSSMUX_CHAT
void listen(chat::Kind kind) {
  assert(chat::service::acquire(kind));
  assert(chat::service::setBackground(kind, true));
  chat::service::release(kind);
  assert(chat::liveSessions == 1);
}
#endif
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 3);
  testStorageRoot = argv[1];
  const std::string test = argv[2];
#if CROSSMUX_CHAT
  if (test != "unregistered") chat::service::tick(false);
#endif
  if (test == "invalid") {
    fixtures("calculator");
    const char* invalidIds[] = {
        nullptr, "", "../calculator", "calculator/other", "_hidden", "UPPER", "abcdefghijklmnopqrstuvwxyzabcdef"};
    for (const char* id : invalidIds) assert(!crossstore::uninstallPackage(id, true));
    assert(storageEvents.empty());
    assert(exists(package("calculator")) && exists(data("calculator") + "/state"));
    assert(crossstore::grantedPermissions("calculator") == 0x3ff);
  } else if (test == "keep" || test == "all" || test == "weread") {
    const std::string id = test == "weread" ? "weread" : "calculator";
    fixtures(id);
    const bool all = test != "keep";
    assert(crossstore::uninstallPackage(id.c_str(), all));
    removed(id, all);
  } else if (test == "storage_unavailable") {
    fixtures("calculator");
    storageReady = false;
    assert(!crossstore::uninstallPackage("calculator", true));
    assert(storageEvents.empty());
  } else if (test == "permission_failure") {
    fixtures("calculator");
    failWrite.insert(permission("calculator") + ".part");
    assert(!crossstore::uninstallPackage("calculator", true));
    assert(exists(package("calculator")) && exists(data("calculator") + "/state"));
    assert(crossstore::grantedPermissions("calculator") == 0x3ff);
  } else if (test == "sidecar_failure") {
    fixtures("calculator");
    failRemove.insert(package("calculator") + ".previous");
    assert(!crossstore::uninstallPackage("calculator", true));
    assert(exists(package("calculator")) && exists(data("calculator") + "/state"));
    failRemove.clear();
    assert(crossstore::uninstallPackage("calculator", true));
    removed("calculator", true);
  } else if (test == "partial_retry") {
    fixtures("calculator");
    failRemove.insert(data("calculator") + "/state.previous");
    assert(!crossstore::uninstallPackage("calculator", true));
    assert(exists(package("calculator")));
    assert(!exists(data("calculator") + "/state"));
    assert(exists(data("calculator") + "/state.previous"));
    failRemove.clear();
    assert(crossstore::uninstallPackage("calculator", true));
    removed("calculator", true);
  } else if (test == "deep" || test == "long_name" || test == "colon_name" || test == "large") {
    fixtures("calculator");
    const auto root = data("calculator");
    if (test == "deep") put(root + "/a/b/c/d/e/f/g/h/i/state");
    if (test == "long_name") put(root + "/" + std::string(80, 'a'));
    if (test == "colon_name") put(root + "/a:outside");
    if (test == "large")
      for (int i = 0; i < 530; ++i) put(root + "/file" + std::to_string(i));
    assert(!crossstore::uninstallPackage("calculator", true));
    assert(exists(package("calculator")));
    retained("calculator");
  } else if (test == "file_instead_of_directory") {
    fixtures("calculator");
    std::filesystem::remove_all(Storage.path(data("calculator").c_str()));
    put(data("calculator"));
    assert(!crossstore::uninstallPackage("calculator", true));
    assert(exists(package("calculator")) && exists(data("calculator")));
  } else if (test == "chat_keep" || test == "chat_all" || test == "rf_all" || test == "esp_all") {
    const std::string id = test == "rf_all" ? "rf_chat" : test == "esp_all" ? "esp_chat" : "wifi_chat";
    fixtures(id);
#if CROSSMUX_CHAT
    const auto kind = test == "rf_all" ? chat::Kind::Radio : test == "esp_all" ? chat::Kind::EspNow : chat::Kind::Wifi;
    listen(kind);
#endif
    const bool all = test != "chat_keep";
    assert(crossstore::uninstallPackage(id.c_str(), all));
    removed(id, all);
    assert(exists("/.crosspoint/" + id) != all);
#if CROSSMUX_CHAT
    assert(chat::liveSessions == 0 && chat::joinedSessions == 1);
    const auto joined = std::find(storageEvents.begin(), storageEvents.end(), "joined:" + id);
    const auto removedPackage = std::find(storageEvents.begin(), storageEvents.end(), "remove:" + package(id));
    assert(joined < removedPackage);
    if (all) {
      const auto erasedFinal =
          std::find(storageEvents.begin(), storageEvents.end(), "remove:/.crosspoint/" + id + "/worker-final-save");
      assert(joined < erasedFinal && erasedFinal < removedPackage);
    } else {
      assert(exists("/.crosspoint/" + id + "/worker-final-save"));
    }
#endif
#if CROSSMUX_CHAT
  } else if (test == "foreground" || test == "wrong_task" || test == "unregistered" || test == "stop_failure") {
    fixtures("wifi_chat");
    if (test == "foreground") assert(chat::service::acquire(chat::Kind::Wifi));
    if (test == "wrong_task") currentTestTask = reinterpret_cast<void*>(uintptr_t{2});
    if (test == "stop_failure") {
      listen(chat::Kind::Wifi);
      failRename.insert("/.crosspoint/chat-background-v2.txt");
    }
    assert(!crossstore::uninstallPackage("wifi_chat", true));
    assert(exists(package("wifi_chat")) && exists(data("wifi_chat") + "/state"));
    assert(exists("/.crosspoint/wifi_chat/identity.tox"));
    assert(exists(package("wifi_chat") + ".previous"));
    if (test == "stop_failure") {
      assert(chat::liveSessions == 1 && chat::service::background(chat::Kind::Wifi));
      failRename.clear();
      assert(crossstore::uninstallPackage("wifi_chat", true));
      removed("wifi_chat", true);
    } else {
      assert(crossstore::grantedPermissions("wifi_chat") == 0x3ff);
    }
    if (test == "foreground") chat::service::release(chat::Kind::Wifi);
#endif
  } else {
    assert(false && "unknown test scenario");
  }
  std::cout << test << " passed\n";
}
