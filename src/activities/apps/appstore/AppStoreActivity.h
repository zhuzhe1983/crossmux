#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "crossstore/PackageIcons.h"
#include "crossstore/StoreCatalog.h"
#include "crossstore/StoreGrid.h"
#include "util/ButtonNavigator.h"

// A tabbed storefront plus drill-down lists. UiAppHost owns rendering/input;
// this is deliberately not a single-list UiListActivity subclass.
class AppStoreActivity final : public Activity, protected UiAppHost {
 public:
  AppStoreActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("AppStore", renderer, input), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return view_ == View::Busy; }

 private:
  enum class View {
    Browser,
    Details,
    Settings,
    Permissions,
    PermissionDetails,
    Capabilities,
    Busy,
    Error,
    ConfirmInstall,
    ConfirmRemove
  };
  enum class Tab { All, Installed };
  enum class Connection { Pending, Connecting, Idle };
  enum class Control : int16_t { All, Installed, Settings, Previous, Next, Refresh };
  static constexpr freeink::ui::ActionId ACTION_ROW = 1, ACTION_APP = 2, ACTION_CONTROL = 3, ACTION_DETAIL = 4,
                                         ACTION_CONFIRM = 5;
  static constexpr size_t PAGE_SIZE = 16;
  static_assert(PAGE_SIZE <= 16);
  using Entry = crossstore::StoreCatalog::Entry;

  crossstore::StoreCatalog catalog_;
  crossstore::PackageIcons icons_;
  // One reusable page, never the complete remote catalog. Activity-owned
  // bounded storage avoids per-frame allocation and SD access.
  Entry entries_[PAGE_SIZE]{};
  Entry lookupEntry_;
  crossstore::Package installedPackage_;
  struct Installed {
    char id[32]{}, version[24]{}, name[96]{};
  };
  Installed installed_[crossstore::MAX_INSTALLED_PACKAGES]{};
  crossstore::InstallMark marks_[PAGE_SIZE]{};
  size_t installedCount_ = 0, count_ = 0, total_ = 0, chosen_ = 0;
  size_t offsets_[2]{}, permissionOffset_ = 0, pageSize_ = 1;
  int focus_ = 3;
  bool keyboardFocus_ = false, morePermissions_ = false;
  bool releasePending_ = false;
  freeink::ui::ListNav nav_;
  ButtonNavigator buttons_;
  freeink::ui::ListItem rows_[PAGE_SIZE + 4]{};
  char labels_[PAGE_SIZE + 4][96]{}, subtitles_[PAGE_SIZE + 4][192]{};
  size_t rowCount_ = 0;
  View view_ = View::Browser;
  Tab tab_ = Tab::All;
  Connection connection_ = Connection::Pending;
  bool wifiStarted_ = false;
  bool resumeIcons_ = false;
  // One bit per visible slot, reset with the page. Background copies of an
  // already loaded bundle must not reallocate/repaint that icon.
  uint16_t catalogIconSlots_ = 0, loadedIconSlots_ = 0;
  uint32_t connectionAt_ = 0, revision_ = 0, iconRevision_ = 0, lastIconPaint_ = 0;
  crossstore::StoreCatalog::Status lastStatus_ = crossstore::StoreCatalog::Status::Empty;
  char source_[256]{}, error_[192]{}, statusText_[128]{}, pageText_[96]{};
  int openRow_ = -1, installRow_ = -1, removeRow_ = -1;
  int primaryRow_ = -1;
  bool primaryFocused_ = false;

  static void screenCallback(UiScreen&, void*);
  static void rowCallback(const freeink::ui::ActionEvent&, void*);
  static void appCallback(const freeink::ui::ActionEvent&, void*);
  static void controlCallback(const freeink::ui::ActionEvent&, void*);
  static void detailCallback(const freeink::ui::ActionEvent&, void*);
  static void confirmationCallback(const freeink::ui::ActionEvent&, void*);
  crossstore::StoreGrid geometry() const;
  Rect listBounds() const;
  void buildScreen(UiScreen&);
  void buildGrid(UiScreen&);
  void buildDetails(UiScreen&);
  void stepDetailsFocus(int);
  void activateDetailsFocus();
  void scrollDetails(int);
  size_t detailListCount() const;
  void showInstallConfirmation();
  void showRemoveConfirmation();
  void confirmInstall();
  void removeConfirmed(bool includeData);
  void buildConfirmation(UiScreen&);
  void confirmationAction(int);
  void stepConfirmationFocus(int);
  void drawChrome();
  void drawFooter();
  bool updateStatus();
  void reloadInstalled();
  int installedIndex(const char*) const;
  void browser(bool preserveSelection = false);
  void loadIcons();
  bool refreshIcons();
  void switchTab(Tab);
  void turnPage(int);
  void control(Control);
  void stepFocus(int);
  void activateFocus();
  void guardHeldButtons();
  bool consumeHeldButtons();
  bool inputActive() const;
  void pumpRefresh();
  void refresh(bool chooseNetwork);
  void stopRefresh();
  void settings();
  void permissions(size_t);
  void permissionApps();
  void details(size_t);
  void install();
  void changeSource();
  void capabilities();
  void fail(const char*);
  void resetRows(View);
  void addRow(const char*, const char* = "");
  bool fetch(const char*, const char*, size_t);
  void activateIndex(int);
  void onBackButton();
};
