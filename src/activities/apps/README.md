# CrossStore 分支说明

本分支的 AppsMenuActivity 从 TF 卡 `/.crosspoint/apps/` 读取安装包，显示已安装应用和独立商店入口。网格/列表跟随设置。传统原生应用说明保留在下方，不能用它描述当前商店启动器的注册方式。

Lua 宿主在 `appstore/`，包校验与权限在 `src/crossstore/`；应用源码和语义图标由相邻 CrossStore 项目管理。游戏画布的规则在 Lua 中，通信协议、后台收听、核心阅读/传输服务仍在固件中。当前已迁移与未迁移功能详见 CrossStore README，API 说明见其 `docs/lua-api.md`。

当前运行时 API 为 5：增加只读的阅读日期/成就查询与微信读书本地章节目录。商店直接进入图标网格，顶部有「全部」「已安装」与设置。勾表示已安装，上箭头表示目录版本较新；按数字组件比较版本，同版/本机较新版不会标成升级。图标保持原灰阶，键盘选择使用描边。

安装与更新的入口是详情页底部固定主按钮。点击后先进入下载确认页，显示应用版本、准确字节数及申请权限；确认前不下载、不授权。卸载也进入独立确认页，提供「删除应用（保留数据）」「删除所有」「取消」，实体键默认选中取消。删除所有仅清理该应用的私有存档/配置、权限及对应聊天身份/记录；保留书籍、导出文件和共享阅读数据。停止聊天服务或清理失败会保留当前应用包供重试；多文件删除不能回滚已经清除的部分数据。

图标角标位于图标内部右下角，白底黑边覆盖图像，灰阶通道也保留它；名称不再为角标腾位置。设置按钮使用可重建的正向 24/32px 设置图标。

商店先从 `/.crosspoint/store-cache/` 读取匹配当前源地址的完整 JSON 快照，再在独立任务中更新；联网失败保留旧内容。首次无缓存时提示联网，已安装页仍可用。已有连接直接复用；Wi-Fi 关闭时尝试上次保存的网络，失败后可在设置中手动刷新/选网，不自动弹走缓存页面。退出商店/睡眠会取消任务；普通页面切换不停止刷新。任务仅随商店活动存在，没有后台 Lua 或常驻商店服务。

静态目录每个 JSON 最多 16 项，完整缓存最多 64 页；UI 按屏幕尺寸每页最多显示 16 格，可跨 JSON 边界翻页。只有当前显示页、最多 64 个已安装条目的简短信息与可见图标驻留 RAM。新目录完整校验后切换两代缓存，失败不会替换旧代；图标用同 hash 的前代缓存过渡。图标预览是独立的固定 7048 字节 CSI1 文件，只有校验后的文件进入缓存；旧目录无预览时仍可安装，已安装包继续提供自己的图标。

较新 API 的有效条目会在详情显示升级固件的原因，不能安装；已安装的兼容旧版仍可打开。目录和启动器只统计合法 `<id>.csp`，忽略 macOS 的 `._` 元数据。刷新不会改变正在进行的点击/按键手势，空闲帧才按应用 ID 保留选中项和页位置。

权限按应用声明并逐项授权；系统能力只读。Mono 木鱼通过 HalBuzzer 播放短音，无蜂鸣器设备继续计数；不强制系统每次操作发声。聊天在清醒时可由原生服务后台收听，休眠停止；后台通知会回到对应 Lua 应用和会话。Lua VM 不常驻后台。

验证：`python3 test/crossstore/test_runtime.py ../CrossStore/packages` 使用实际 Lua VM 检查沙箱、配额、游戏持久化、四种画布尺寸与完整聊天快照。构建 `papermono_store` / `papers3_store` 后仍需分别实测触摸、声音、网络和休眠；宿主测试不代表硬件验收。

`test/crossstore/test_catalog.py` 验证目录/安装元信息的不同兼容性策略及分页。相邻 CrossStore 检出存在时，CMake 还注册 `CrossStoreGames`、`CrossStoreReadingStats`、`CrossStoreWeReadCache`；非默认位置可通过 `-DCROSSSTORE_PACKAGES_DIR=/path/to/packages` 指定。新包更新与固件更新是两个独立步骤。

网格/输入测试为 `test/crossstore/test_store_grid.py`，真实图标加载/灰阶通路测试为 `test/crossstore/test_package_icons.py`，目录/网络故障注入测试为 `test/store_catalog/test_store_catalog.py`。刷新任务使用 8 KiB 栈、一次复用的有界 scratch 和一页 JSON；S3 的目录 JSON 与最多 5248 字节图标任务表使用 PSRAM。下载与 JSON 解析错开，单次 HTTP 返回即释放 TLS；4 灰阶图标最多 16 KiB，16 灰阶最多 72 KiB 且必须位于 PSRAM，没有第二份全屏缓存。网络读/连接超时配置为 3 秒，单文件截止检查 12 秒；SDK DNS 解析不能立即取消，退出等待不承诺严格的 3 秒上限。实机需验证断网开店、两页切换、安装/卸载/升级角标、按住图标时目录更新，以及刷新期间返回/Home/休眠。

---

# Apps

The `apps/` directory holds all non-reader sub-applications shipped on CrossMux. They use the Apps launcher (`AppsMenuActivity`) and the conventions below; the main-tab and home navigation expose that launcher.

The directory groups app implementations such as games, tools, reading analytics, and WeRead. The launcher also links to core workflows such as file transfer and OPDS; appearing in Apps does not require moving those implementations into this directory.

---

## Directory layout

```
apps/
├── AppsMenuActivity.{h,cpp}   # dispatcher — see "Adding a new app" below
├── GameUi.{h,cpp}             # shared helpers, game-only (centering math, elapsed-time format)
├── GameSaveDebouncer.h        # 1.5s save debounce, used by sudoku/gomoku/minesweeper
├── airpage/                    # cloud image display app
├── sudoku/                    # one subdirectory per app, files keep the app-name prefix
├── gomoku/
├── chinese-chess/             # hidden by default
├── minesweeper/
├── woodfish/                  # electronic woodfish with lazy SD checkpointing
└── avatar/
```

**Why the `Game*` prefix for `GameUi` and `GameSaveDebouncer`** — these helpers carry save-state and game-board semantics. They are used by Sudoku, Gomoku, and Minesweeper, not by Ugly Avatar (which is a single-shot generator). The name reflects what they actually do; do not rename them to `App*`.

**Why nested rather than flat** — apps share the "Apps" launcher concept and should be groupable. Reader and Settings are top-level features, so they sit flat at `activities/<feature>/`. This is a deliberate structural difference, not an inconsistency.

**File naming inside an app directory** — keep the app-name prefix (`SudokuBoard.cpp`, not `Board.cpp`). The redundancy is intentional: grep, IDE search, and crash log lines stay unambiguous. No `namespace` wrapping for the same reason — class names are globally unique.

---

## Adding a new app

The dispatcher is table-driven. A new app needs the app files, a navigation method, and one catalog entry:

### 1. Create the app

```
apps/<myapp>/
  MyAppActivity.{h,cpp}   # required — extends Activity
  ...                     # game-specific board / store / generator as needed
```

The activity's `loop()` must handle Back by returning to the Apps menu, not the home screen:

```cpp
if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
  activityManager.goToApps();
}
```

### 2. Add a navigation method to `ActivityManager`

In `src/activities/ActivityManager.{h,cpp}`:

```cpp
// .h
void goToMyApp();

// .cpp
#include "apps/myapp/MyAppActivity.h"
void ActivityManager::goToMyApp() {
  replaceActivityWith<MyAppActivity>();
}
```

`replaceActivityWith<T>(...)` and `startActivityForResultWith<T>(handler, ...)`
prepend `renderer` and `mappedInput` to the constructor arguments, allocate with
`makeUniqueNoThrow`, log OOM, and leave the current Activity in place on
failure. Do not construct Activities with `std::make_unique`.

### 3. Assign an ID and append one row to `kAppEntries`

Append the ID in `src/AppVisibility.h` and the row in `apps/AppsMenuActivity.cpp`; this is an excerpt, not a replacement catalog:

```cpp
enum class AppId : uint8_t {
  Sudoku = 2,
  Gomoku = 3,
  Minesweeper = 5,
  UglyAvatar = 7,
  MyApp = 17,  // example: use the next unused bit ID in the current catalog
  Count = 18,
};

constexpr AppEntry kAppEntries[] = {
    {AppId::Sudoku,      StrId::STR_SUDOKU_TITLE,      UIIcon::Sudoku,      &ActivityManager::goToSudoku},
    {AppId::Gomoku,      StrId::STR_GOMOKU_TITLE,      UIIcon::Gomoku,      &ActivityManager::goToGomoku},
    {AppId::Minesweeper, StrId::STR_MINESWEEPER_TITLE, UIIcon::Minesweeper, &ActivityManager::goToMinesweeper},
    {AppId::UglyAvatar,  StrId::STR_UGLY_AVATAR,       UIIcon::Avatar,      &ActivityManager::goToUglyAvatar},
    {AppId::MyApp,       StrId::STR_MYAPP_TITLE,       UIIcon::MyApp,       &ActivityManager::goToMyApp},
};
```

Define stable IDs and default visibility in `src/AppVisibility.h`; keep titles, icons, and launch actions in the menu table.

The ID is the persisted bit position in `hiddenAppsMask`: allocate the next unused value, never reuse or change existing
values, and keep conditional-app IDs outside their `#ifdef`. New bits default to visible. Fresh settings hide Chinese
Chess, Minesweeper, 2048, Ugly Avatar, Buddy, Sokoban, Pixel Switch, and Woodfish.
Existing masks are preserved except for the one-time Buddy migration from catalog version 0.
The menu, launcher, and App Visibility settings all read this same table; no `switch` or `buildItems()` is needed.
The visibility mask is 32-bit. `Calculator = 15` and `Woodfish = 16` are stable;
IDs 17 through 31 remain available.

### 4. Add the i18n key and icon

- **i18n**: add `STR_MYAPP_TITLE: "My App"` to `lib/I18n/translations/english.yaml`. Other languages fall back to English; translate selectively. Then run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` once locally (regenerated at build time too).
- **Icon**: add `MyApp` to the `UIIcon` enum in `src/components/themes/BaseTheme.h` and add both 32×32 1-bit variants: the pre-rotated Lyra bitmap selected by `LyraTheme::iconForName(size==32)`, and the canonical-orientation bitmap selected by `InxAppIcons::get()`. No 24px bitmap is needed.

The replacement [semantic app icon pack](../../../assets/app-icons/semantic-v1/README.md)
contains 18 color masters plus 2/4/16-level PNG, packed binary and C++ variants at
32/48/64/96/128px. `scripts/prepare_app_icons.py` regenerates these assets and
`scripts/verify_app_icons.py` checks their on-disk consistency. These files are
prepared resources; the current theme renderers still use the icons described above.

---

### 5. (Optional) Stateless toy apps

Some apps have no save state at all. Ugly Avatar is a single-screen generator that creates a new avatar on entry and exits cleanly. It skips both `GameSaveDebouncer` and any `*Store.{h,cpp}` layer, and its `Activity` is launched directly (no `*MenuActivity`). Use this pattern when the app has no "in-progress game" worth resuming; it cuts a few hundred lines and avoids persistent writes.

### Reusable implementation patterns

Choose the smallest pattern that fits:

| App shape | Reference | Reuse |
|---|---|---|
| Stateless, single screen | `avatar/UglyAvatarActivity` | `Activity`, `GameUi` action geometry; no Store or menu Activity |
| Stateful, single screen | `2048/Game2048Activity`, `woodfish/WoodfishActivity` | Board/state object plus `GameSaveDebouncer` or an app-specific idle checkpoint |
| Board game with launcher | `sudoku/` | Separate Board, Store, MenuActivity, GameActivity; `OptionPopup` for in-game menus |

Use `OptionPopup` for in-game action and difficulty menus. It owns touch hit
testing, logical-button navigation, and the inherited-input barrier. Build its
labels when the popup opens, not during rendering. Keep rules, win detection,
AI, and persistence in the game: they are not framework concerns.

---

### 6. Language-independent visibility

Chinese Chess and WeRead are compiled into the unified firmware. The existing
`ENABLE_CHINESE_VERSION` compatibility guards remain enabled by the base and
simulator profiles; do not add a separate Chinese build or source-filter split.

The launcher applies `effectiveHiddenMask()` to combine user visibility and
configured OPDS servers. The settings toggle reflects the user choice even when
OPDS is temporarily absent from the menu because no server is configured. App visibility is independent of the UI language and
content profile. WeRead is visible by default in every language; Chinese Chess
is hidden by default. Language changes preserve the user's app visibility choices.
See [Chinese support](../../../docs/engineering/chinese-build.md).

---

## UI conventions

- **Launcher layout**: `buildScreen()` uses the shared `UiScreen`/FreeInkUI list, or the existing icon-grid layout selected by `usesIconLayout()`. Reuse the list viewport and `InxGridGeometry` for navigation and touch geometry.
- **Pagination**: the list viewport and icon grid manage their own visible slices. Use the shared navigation helpers and theme scrollbar rather than duplicating row counts or screen dimensions.
- **Header**: each app draws its own header via `GUI.drawHeader(... tr(STR_<APP>_TITLE))`.
- **Result layouts**: center multi-line status/result blocks from measured font heights with `gameCenteredBlockY`; reserve the title bar and button-hint area instead of relying on fixed Y offsets.
- **Back button labels**: the four button hints follow the project standard — `STR_BACK / STR_SELECT / STR_DIR_UP / STR_DIR_DOWN` for menu rows; app-specific actions for in-game screens.

## Navigation flow

```
Home  ──Confirm "Apps"──▶  AppsMenu  ──Confirm row──▶  <App>
  ▲                            │
  └──────Back──────────────────┘    ◀──Back──  <App>  (returns to AppsMenu, not Home)
```

Every sub-app's Back button must call `activityManager.goToApps()`. This mirrors how Sudoku, Gomoku, Ugly Avatar, and AirPage behave.

Electronic Woodfish accepts Confirm and all four logical directions on button
release. Touch devices add taps on the rendered wooden body; whitespace,
mallet, ripples, drags, and the system Back gesture are not knocks. Its
`uint32_t` counter saturates, has no reset action, and is checkpointed to SD
after 60 seconds idle or on exit.

## AirPage

AirPage always enters on its QR page and stays offline until Refresh or live
mode needs Wi-Fi. Network-dependent apps use the shared Wi-Fi picker on demand;
cancelling it leaves the app on its current screen so the user can retry.
Its mapped bottom actions are Back, Settings, Images, and Refresh; logical
previous/next also map the side buttons to Images/Refresh in every orientation.
Touch devices render those QR-page actions as a tappable footer, and tapping a
full-screen image opens the same actions plus Set Cover in a modal menu.
Connecting or reconnecting never downloads an image by itself; only Refresh or
a live MQTT push starts a download.
Settings uses the standard themed list for manual/live mode and the optional
"set downloads as sleep screen" toggle. Images opens a newest-first list of the
current image plus up to 19 archived deliveries; selecting one displays it
full-screen, where Confirm offers the standard sleep-screen confirmation and
Back returns to the QR page.

Live mode runs only while AirPage is foregrounded, backs off failed
connections, and allows normal auto-sleep again after the retry window expires.
Manual mode keeps foreground Wi-Fi available but permits idle auto-sleep.
Downloads are identified by signature and accept BMP or JPEG from the same
endpoint. Exact duplicates reuse the current entry. Unique downloads are
validated transactionally before the old image is archived. The QR uses physical
height/width for portrait upload dimensions and `mode=gray4` or `mode=gray16`
from `GfxRenderer::getGrayscaleLevels()`. Legacy boards and the simulator default
to four levels; Read Pico explicitly advertises sixteen.

Four-level JPEG output retains the EPUB aspect-fit, dithering and streamed pixel
cache. Read Pico decodes BMP/JPEG once from the original file into the existing
native 4bpp framebuffer, bypassing four-level quantization and the pixel cache.
Gray8 brightness maps to `(gray + 8) / 17`; the renderer applies the existing
orientation transform. A B/W proxy remains available for modal menus. Closing a
menu, or selecting a historical original, re-renders the native image. Decode or
refresh failure rejects the new image through the existing rollback transaction;
a four-level substitute is never reported as native sixteen-level success.

Set Cover copies BMP originals unchanged. JPEG converts to an 8-bit grayscale BMP
on a sixteen-level board and the existing 2-bit BMP elsewhere, fit without
cropping. Header, palette and row short writes fail conversion. Installation
retains `/sleep.bmp.part` and `/sleep.bmp.bak`, validating the generated file and
rolling back on replacement or settings-save failure. Custom sleep images use
native output only on a sixteen-level board with No Filter; failure displays the
default sleep screen. Book covers, text AA, alpha sleep overlays and filters keep
their existing paths.

The native BMP scratch is bounded to one Gray8 row plus one source row (at most
10,240 bytes); it is fallible and released with the Bitmap. JPEG reuses its
existing decoder and bounded row/MCU workspaces, without another full-screen
buffer or a new cache format. `python3 scripts/tests/test_native_grayscale.py`
compiles the production renderer, decoders and AirPage transactions with recording
HAL seams and checks the sleep methods in isolation. Firmware builds and these
host checks do not establish panel polarity, tone separation, ghosting, or physical
sleep/wake acceptance; see `docs/engineering/read-pico.md`.

## Resource budget

Apps share the reader's resource budget: about 380KB usable RAM on ESP32-C3, with target-specific budgets on S3. Specifically:

- **Heap**: allocate at `onEnter()`, free at `onExit()` (Activities are heap-allocated and `delete`d on exit). Don't hold buffers across navigation.
- **Stack**: keep local function variables under 256 bytes; large buffers go on heap or `static`.
- **Flash strings**: large constant tables must be `static constexpr` to stay in flash, not in DRAM.
- **Storage writes**: never save on every user interaction. Debounce save-on-activity-exit, or use `GameSaveDebouncer` (1.5s window). Electronic Woodfish checkpoints its SD-backed counter only after 60 seconds idle or on exit.
- **Single-buffer framebuffer**: 48KB framebuffer is shared. If an app needs to overlay (modal save UI etc.), use `renderer.storeBwBuffer()` / `restoreBwBuffer()` — see `UglyAvatarActivity::onSave()` for a worked example.

See [AGENTS.md](../../../AGENTS.md) and the
[resource protocol](../../../docs/engineering/hardware-constraints.md#the-resource-protocol);
apps are not exempt.

## Experimental native chat

Opt-in `papermono_chat` builds provide `rf_chat` and `wifi_chat`; `papers3_chat`
provides Wi-Fi chat. Apps combine hardware requirements with user visibility,
so a device cannot enable a missing radio through the visibility setting.
See [chat apps](../../../docs/engineering/chat-apps.md) for protocol, resource,
storage and acceptance details.

The experimental chat builds also provide optional **Listen in background**
under each app’s **Discover & settings**. Awake listeners post coalesced system
messages while reading; the badge opens the corresponding conversation. Sleep
and USB Drive stop listeners. See [chat lifecycle and limits](../../../docs/engineering/chat-apps.md).
