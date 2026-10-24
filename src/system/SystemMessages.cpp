#include "SystemMessages.h"

#include <FreeInkUIGfxRenderer.h>
#include <I18n.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

#include "components/UIScale.h"
#include "components/UITheme.h"
#include "fontIds.h"
#if CROSSMUX_STORE
#include "crossstore/Permissions.h"
#endif
namespace systemMessages {
namespace {
// Eight coalesced conversation notices, about 2.2 KiB only in opt-in chat builds.
Inbox inbox;
StaticSemaphore_t mutexStorage;
SemaphoreHandle_t mutex() {
  static auto value = xSemaphoreCreateMutexStatic(&mutexStorage);
  return value;
}
std::atomic<uint32_t> count{0};
std::atomic<uint32_t> generation{0};
Rect badge{};
bool allowed = false;
void publish() {
  count.store(inbox.unread());
  generation.store(inbox.revision);
}
}  // namespace
void post(Source source, const char* peer, const char* preview, NoticeKind kind) {
#if CROSSMUX_STORE
  const char* owner = nullptr;
  switch (source) {
    case Source::Radio:
      owner = "rf_chat";
      break;
    case Source::Wifi:
      owner = "wifi_chat";
      break;
    case Source::EspNow:
      owner = "esp_chat";
      break;
    case Source::Application:
      owner = peer;
      break;
  }
  if (!crossstore::permissionGranted(owner, "notifications")) return;
#endif
  xSemaphoreTake(mutex(), portMAX_DELAY);
  inbox.post(source, peer, preview, kind);
  publish();
  xSemaphoreGive(mutex());
}
void read(Source source, const char* peer) {
  xSemaphoreTake(mutex(), portMAX_DELAY);
  inbox.read(source, peer);
  publish();
  xSemaphoreGive(mutex());
}
void copy(Inbox& out) {
  xSemaphoreTake(mutex(), portMAX_DELAY);
  out = inbox;
  xSemaphoreGive(mutex());
}
uint32_t revision() { return generation.load(); }
uint32_t unread() { return count.load(); }
void beginFrame(bool enabled) {
  allowed = enabled;
  clearHit();
}
void clearHit() { badge = Rect{}; }
bool hit(int x, int y) {
  return badge.width > 0 && x >= badge.x && x < badge.x + badge.width && y >= badge.y && y < badge.y + badge.height;
}
void drawBadge(const GfxRenderer& renderer, const Rect& band) {
  if (!allowed || !unread() || band.width < 160) return;
  namespace fui = freeink::ui;
  const auto spec = uiScaleSpec(false);
  fui::GfxRendererFrame<1> ui(renderer, spec.smallFontId, spec.bodyFontId, spec.titleFontId);
  const auto& theme = UITheme::getInstance().getMetrics();
  const int width = std::min(200, band.width - 2 * theme.contentSidePadding);
  const int height = std::min(band.height, std::max(44, ui.target.lineHeight(fui::GfxRendererTarget::FONT_SMALL) + 12));
  if (height <= 0) return;
  badge = Rect{band.x + (band.width - width) / 2, std::max(0, band.y + (band.height - height) / 2), width, height};
  char label[64];
  std::snprintf(label, sizeof(label), tr(STR_SYSTEM_MESSAGES_COUNT), static_cast<unsigned>(unread()));
  fui::TextStyle style;
  style.font = fui::GfxRendererTarget::FONT_SMALL;
  style.align = fui::TextAlign::Center;
  const fui::Rect rect{static_cast<int16_t>(badge.x), static_cast<int16_t>(badge.y), static_cast<int16_t>(badge.width),
                       static_cast<int16_t>(badge.height)};
  ui.target.fill(rect, fui::Paint::solid(fui::Color::White), 8);
  ui.target.stroke(rect, fui::Paint::solid(fui::Color::Black), 2, 8);
  ui.target.text(rect, label, style);
}
}  // namespace systemMessages
