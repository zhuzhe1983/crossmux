#pragma once

#include <I18n.h>
#include <Memory.h>

#include <array>
#include <vector>

#include "activities/UiListActivity.h"
#include "crossstore/Package.h"
#include "crossstore/PackageIcons.h"

// Installed-package launcher; the store is a separate entry.
class AppsMenuActivity final : public UiListActivity {
 public:
  AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("AppsMenu", renderer, mappedInput) {}
  ~AppsMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  MainTab mainTab() const override { return MainTab::Apps; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;

  static int getAppCount();
  static StrId getAppTitleId(int appIndex);
  static bool isAppVisible(int appIndex);
  static bool isAppSupported(int appIndex);
  static bool setAppVisible(int appIndex, bool visible);

 private:
  int getVisibleAppCount() const { return static_cast<int>(installedCount + 1); }
  int getAppIndexForVisibleIndex(int visibleIndex) const {
    return visibleIndex >= 0 && visibleIndex < getVisibleAppCount() ? visibleIndex : -1;
  }
  int listCount() const override { return getVisibleAppCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void drawChrome() override;
  void drawFooter() override;
  void beginDisplayFrame() override;
  void displayFrame() override;
  void paintIcon(int appIndex, int x, int y, int size);
  static void paintListIcon(void* ctx, const freeink::ui::Rect& rect, const freeink::ui::ListItem& item,
                            freeink::ui::Paint foreground);
  bool usesIconLayout() const;
  Rect appContentRect() const;
  void rebuildRowItems();
  void openSelected();
  bool handleCustomInput() override;
  bool handleListSwipe(MappedInputManager::SwipeDir swipe, int x, int y) override;
  void guardGridButtons();
  void turnIconPage(int direction);
  bool gridButtonReleasePending = false;
  void drawIconGrid(UiScreen& screen, const Rect& rect, int visibleCount, bool showSelection);

  crossstore::PackageIcons icons;

  // Allocated once on entry and reused for every repaint. The app catalog is
  // bounded to 64 installed entries plus the store; dynamic storage avoids pinning the maximum in every
  // live Apps activity while keeping render allocation-free.
  std::vector<freeink::ui::ListItem> rowItems;
  static constexpr size_t MAX_INSTALLED = crossstore::MAX_INSTALLED_PACKAGES;
  struct InstalledEntry {
    char id[32]{}, name[96]{};
  };
  // Metadata remains bounded; icon bytes are loaded only for the visible page.
  std::array<InstalledEntry, MAX_INSTALLED> installedApps{};
  size_t installedCount = 0;
  int iconCacheStart = -1;
  void reloadInstalled();
  void loadPageIcons();
  const char* appTitle(int index) const;
};
