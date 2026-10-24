#pragma once
#include <HalStorage.h>

#include "ChatModel.h"

namespace chat {
inline unsigned liveSessions = 0, joinedSessions = 0;
inline constexpr const char* ids[] = {"rf_chat", "wifi_chat", "esp_chat"};
class Session {
 public:
  explicit Session(Kind kind) : kind_(kind) { ++liveSessions; }
  ~Session() {
    const auto* id = ids[static_cast<unsigned>(kind_)];
    storageEvents.emplace_back("joined:" + std::string(id));
    // Real stop() waits for the worker to finish its final identity/history
    // save. This boundary emulates that final write before returning.
    const auto path = std::string("/.crosspoint/") + id + "/worker-final-save";
    auto file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (file) file.write("saved", 5);
    --liveSessions;
    ++joinedSessions;
  }
  bool start() { return true; }
  bool active() const { return true; }
  void viewing(const char*) {}

 private:
  Kind kind_;
};
}  // namespace chat
