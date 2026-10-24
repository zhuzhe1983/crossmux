#include "SystemMessagesActivity.h"

#include <I18n.h>

#include <cstdio>

#if CROSSMUX_CHAT
#include "activities/apps/chat/ChatActivity.h"
#endif
#include "activities/apps/appstore/LuaAppActivity.h"
#include "components/UITheme.h"
#include "system/SystemMessages.h"
const char* SystemMessagesActivity::headerTitle() const { return tr(STR_SYSTEM_MESSAGES); }
int SystemMessagesActivity::listCount() const { return inbox_.count; }
void SystemMessagesActivity::update() {
  closeRouting();
  systemMessages::copy(inbox_);
  for (size_t i = 0; i < inbox_.count; ++i) {
    const auto& notice = inbox_.items[i];
    const bool publicRoom = !std::strcmp(notice.peer, "*");
    const char* peer = publicRoom ? tr(STR_CHAT_PUBLIC_ROOM) : notice.peer;
    const char* appName = "";
    switch (notice.source) {
      case systemMessages::Source::Radio:
        appName = tr(STR_RF_CHAT);
        break;
      case systemMessages::Source::Wifi:
        appName = tr(STR_WIFI_CHAT);
        break;
      case systemMessages::Source::Application:
        appName = notice.peer;
        break;
      case systemMessages::Source::EspNow:
        appName = tr(STR_ESP_CHAT);
        break;
    }
    std::snprintf(labels_[i], sizeof(labels_[i]), "%s · %.*s · %u", appName,
                  publicRoom ? static_cast<int>(std::strlen(peer)) : 8, peer, notice.unread);
    rows_[i] = {};
    rows_[i].label = labels_[i];
    switch (notice.kind) {
      case systemMessages::NoticeKind::Message:
        rows_[i].subtitle = notice.preview;
        break;
      case systemMessages::NoticeKind::FriendRequest:
        rows_[i].subtitle = tr(STR_CHAT_ACCEPT_REQUEST);
        break;
    }
    rows_[i].actionValue = i;
  }
}
void SystemMessagesActivity::onEnter() {
  UiListActivity::onEnter();
  RenderLock lock;
  update();
}
void SystemMessagesActivity::loop() {
  if (millis() - refreshAt_ >= 1000) {
    refreshAt_ = millis();
    RenderLock lock;
    if (inbox_.revision != systemMessages::revision()) {
      update();
      requestUpdate();
    }
  }
  UiListActivity::loop();
}
void SystemMessagesActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin({static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                           static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  if (!inbox_.count) {
    screen.centeredText(tr(STR_SYSTEM_MESSAGES_EMPTY));
    return;
  }
  fui::ListProps props;
  props.items = rows_;
  props.count = inbox_.count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
void SystemMessagesActivity::activateIndex(int index) {
  char peer[77];
  systemMessages::Source source;
  {
    RenderLock lock;
    if (index < 0 || index >= inbox_.count) return;
    std::strcpy(peer, inbox_.items[index].peer);
    source = inbox_.items[index].source;
    app.clearTapFlash();
  }
  if (source == systemMessages::Source::Application) {
    if (startActivityForResultWith<LuaAppActivity>([](const ActivityResult&) {}, peer))
      systemMessages::read(source, peer);
    return;
  }
#if CROSSMUX_CHAT
  chat::Kind kind = chat::Kind::Radio;
  switch (source) {
    case systemMessages::Source::Application:
      return;
    case systemMessages::Source::Radio:
      kind = chat::Kind::Radio;
      break;
    case systemMessages::Source::Wifi:
      kind = chat::Kind::Wifi;
      break;
    case systemMessages::Source::EspNow:
      kind = chat::Kind::EspNow;
      break;
  }
#if CROSSMUX_STORE
  const char* id = kind == chat::Kind::Radio ? "rf_chat" : kind == chat::Kind::Wifi ? "wifi_chat" : "esp_chat";
  startActivityForResultWith<LuaAppActivity>([](const ActivityResult&) {}, id, peer);
#else
  startActivityForResultWith<ChatActivity>([](const ActivityResult&) {}, kind, peer, true);
#endif
#endif
}
