#include <Logging.h>
#include <WiFi.h>

#include <cstring>

#include "LuaAppActivity.h"
#include "crossstore/Permissions.h"
#include "system/SystemMessages.h"
#if CROSSMUX_CHAT
#include "activities/apps/chat/ChatService.h"
#include "activities/apps/chat/ChatSession.h"

void LuaAppActivity::releaseChat() {
  if (chatSession_)
    ::chat::service::release(chatKind_);
  else if (chatWifiOwned_) {
    ::chat::service::ownWifi();
    ::chat::service::release(::chat::Kind::Wifi);
  }
  chatWifiOwned_ = false;
  chatSession_ = nullptr;
  chatSnapshot_.reset();
}
#endif

bool LuaAppActivity::chat(const char* command, const char* argument, int64_t index, const char* text,
                          crossstore::ServiceReply& reply) {
#if CROSSMUX_CHAT
  namespace svc = ::chat::service;
  if (!std::strcmp(command, "start")) {
    if (chatSession_) return true;
    if (!std::strcmp(argument, "radio"))
      chatKind_ = ::chat::Kind::Radio;
    else if (!std::strcmp(argument, "wifi"))
      chatKind_ = ::chat::Kind::Wifi;
    else if (!std::strcmp(argument, "esp_now"))
      chatKind_ = ::chat::Kind::EspNow;
    else
      return false;
    const bool wifi = chatKind_ == ::chat::Kind::Wifi;
    if (!allowed(wifi ? "network" : "radio") || !capability(wifi                              ? "tox"
                                                            : !std::strcmp(argument, "radio") ? "lora"
                                                                                              : "esp_now"))
      return false;
    if (wifi && WiFi.status() != WL_CONNECTED) {
      chatWifiOwned_ = chatWifiOwned_ || WiFi.getMode() == WIFI_OFF;
      wifiPending_ = true;
      return false;
    }
    // One snapshot is shared by all queries in a Lua frame; no per-message
    // allocation, no protocol or worker pointers exposed to an application.
    chatSnapshot_ = makeUniqueNoThrow<::chat::Snapshot>();
    if (!chatSnapshot_) {
      LOG_ERR("STORE", "OOM chat snapshot");
      return false;
    }
    if (chatWifiOwned_) svc::ownWifi();
    chatSession_ = svc::acquire(chatKind_);
    if (!chatSession_) {
      chatSnapshot_.reset();
      return false;
    }
    return chatSession_->copySnapshot(*chatSnapshot_);
  }
  if (!chatSession_ || !chatSnapshot_) return false;
  if (!allowed(chatKind_ == ::chat::Kind::Wifi ? "network" : "radio")) {
    releaseChat();
    return false;
  }
  auto& snapshot = *chatSnapshot_;
  if (!std::strcmp(command, "snapshot")) {
    if (!chatSession_->copySnapshot(snapshot)) return false;
    constexpr const char* states[] = {"stopped", "starting", "online", "connecting", "failed", "storage_error"};
    const auto state = static_cast<size_t>(snapshot.status);
    if (state >= std::size(states)) return false;
    std::snprintf(reply.text, sizeof(reply.text), "%s", states[state]);
    std::snprintf(reply.extra, sizeof(reply.extra), "%s", snapshot.identity);
    reply.a = snapshot.revision;
    reply.b = snapshot.contactCount;
    reply.c = snapshot.messageCount;
    return true;
  }
  if (!std::strcmp(command, "contact")) {
    if (index < 1 || index > snapshot.contactCount) return false;
    const auto& contact = snapshot.contacts[index - 1];
    std::snprintf(reply.text, sizeof(reply.text), "%s", contact.id);
    reply.a = contact.online;
    reply.b = contact.request;
    return true;
  }
  if (!std::strcmp(command, "nearby_count")) {
    reply.a = snapshot.nearby.count;
    reply.b = snapshot.nearby.active;
    reply.c = snapshot.channel;
    return true;
  }
  if (!std::strcmp(command, "nearby")) {
    if (index < 1 || index > snapshot.nearby.count) return false;
    ::chat::hex(snapshot.nearby.peers[index - 1].address, ::chat::nearby::ADDRESS_BYTES, reply.text);
    return true;
  }
  if (!std::strcmp(command, "message")) {
    if (index < 1 || index > snapshot.messageCount) return false;
    const auto& message = snapshot.messages[index - 1];
    std::snprintf(reply.text, sizeof(reply.text), "%s", message.text);
    std::snprintf(reply.extra, sizeof(reply.extra), "%s", message.peer);
    reply.a = static_cast<int32_t>(message.delivery);
    reply.b = message.id;
    reply.c = message.receipts;
    return true;
  }
  if (!std::strcmp(command, "add_result")) {
    reply.a = static_cast<int32_t>(snapshot.addResult);
    return true;
  }
  if (!std::strcmp(command, "send")) return chatSession_->send(argument, text);
  if (!std::strcmp(command, "add")) return chatSession_->add(argument);
  if (!std::strcmp(command, "retry")) return chatSession_->retry(argument, static_cast<uint32_t>(index));
  if (!std::strcmp(command, "discover")) return chatSession_->discover();
  if (!std::strcmp(command, "viewing")) {
    chatSession_->viewing(argument);
    systemMessages::Source source = systemMessages::Source::Radio;
    switch (chatKind_) {
      case ::chat::Kind::Radio:
        break;
      case ::chat::Kind::Wifi:
        source = systemMessages::Source::Wifi;
        break;
      case ::chat::Kind::EspNow:
        source = systemMessages::Source::EspNow;
        break;
    }
    systemMessages::read(source, argument);
    return true;
  }
  if (!std::strcmp(command, "background")) {
    if (argument[0]) {
      const bool enabled = !std::strcmp(argument, "on");
      if (enabled && !allowed("background")) return false;
      if (!svc::setBackground(chatKind_, enabled)) return false;
    }
    reply.a = svc::background(chatKind_);
    return true;
  }
#else
  (void)command;
  (void)argument;
  (void)index;
  (void)text;
  (void)reply;
#endif
  return false;
}
