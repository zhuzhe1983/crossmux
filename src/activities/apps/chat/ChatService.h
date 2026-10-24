#pragma once
#include "ChatBuildConfig.h"
#if CROSSMUX_CHAT
#include "ChatModel.h"
namespace chat {
class Session;
// Only the main/activity task mutates ownership. Workers own their sessions'
// transport and storage; foreground views borrow a session until release().
namespace service {
Session* acquire(Kind kind);
void release(Kind kind);
bool background(Kind kind);
bool setBackground(Kind kind, bool enabled);
// Uninstall is owned by the same activity task that calls tick(). Refuse while
// a foreground view borrows the session; stop joins its worker before success.
bool canUninstall(Kind kind);
bool stopForUninstall(Kind kind);
void ownWifi();
void tick(bool allowWifiJoin);
void stopForSleep();
}  // namespace service
}  // namespace chat
#endif
