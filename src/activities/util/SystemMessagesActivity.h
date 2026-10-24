#pragma once
#include "ChatBuildConfig.h"
#include "activities/UiListActivity.h"
#include "system/MessageInbox.h"
class SystemMessagesActivity final : public UiListActivity {
 public:
  SystemMessagesActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("SystemMessages", renderer, input) {}
  void onEnter() override;
  void loop() override;

 private:
  systemMessages::Inbox inbox_;
  freeink::ui::ListItem rows_[systemMessages::Inbox::CAPACITY]{};
  char labels_[systemMessages::Inbox::CAPACITY][80]{};
  uint32_t refreshAt_ = 0;
  void update();
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
};
