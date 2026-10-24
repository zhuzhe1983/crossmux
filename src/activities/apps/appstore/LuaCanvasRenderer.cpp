#include <algorithm>
#include <cstring>

#include "LuaAppActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;
namespace {
fui::Color shade(uint8_t gray) {
  if (gray <= 2) return fui::Color::Black;
  if (gray <= 7) return fui::Color::DarkGray;
  if (gray <= 12) return fui::Color::LightGray;
  return fui::Color::White;
}
}  // namespace

void LuaAppActivity::buildCanvas(UiScreen& screen) {
  syncPagedViewport(std::max(1, static_cast<int>(canvas_->controlCount)));
  auto& target = screen.target();
  const auto bounds = canvasBounds();
  const GfxRenderer::ClipScope clip(renderer, bounds.x, bounds.y, bounds.width, bounds.height);
  // A dedicated large-font slot leaves all theme-bound body/header fonts intact.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_LABEL, CONTROL_18_FONT_ID);
  for (size_t i = 0; i < canvas_->count; ++i) {
    const auto& command = canvas_->commands[i];
    const fui::Rect rect{static_cast<int16_t>(bounds.x + command.x), static_cast<int16_t>(bounds.y + command.y),
                         command.width, command.height};
    const char* label = canvas_->text + command.text;
    const auto paint = fui::Paint::solid(shade(command.gray));
    switch (command.kind) {
      case crossstore::DrawKind::Box:
      case crossstore::DrawKind::Circle: {
        const int radius =
            command.kind == crossstore::DrawKind::Circle ? std::min(command.width, command.height) / 2 : command.radius;
        if (command.style)
          target.stroke(rect, paint, command.style, radius);
        else
          target.fill(rect, paint, radius);
        break;
      }
      case crossstore::DrawKind::Line:
        target.line({rect.x, static_cast<int16_t>(rect.y + (command.radius ? rect.height - 1 : 0))},
                    {static_cast<int16_t>(rect.x + rect.width - 1),
                     static_cast<int16_t>(rect.y + (command.radius ? 0 : rect.height - 1))},
                    std::max<uint8_t>(1, command.style), paint);
        break;
      case crossstore::DrawKind::Text: {
        auto style = screen.theme().bodyText;
        switch (command.font) {
          case 0:
            style = screen.theme().smallText;
            break;
          case 1:
            break;
          case 2:
            style = screen.theme().titleText;
            style.bold = true;
            break;
          case 3:
            style.font = fui::GfxRendererTarget::FONT_LABEL;
            style.bold = true;
            break;
        }
        style.align = static_cast<fui::TextAlign>(command.align);
        style.color = shade(command.gray);
        style.maxLines = std::max(1, rect.height / std::max(1, static_cast<int>(target.lineHeight(style.font))));
        target.text(rect, label, style);
        break;
      }
      case crossstore::DrawKind::Button: {
        const bool dark = command.style == 3;
        const auto background = dark                 ? fui::Color::Black
                                : command.style == 2 ? fui::Color::LightGray
                                                     : fui::Color::White;
        const auto foreground = dark ? fui::Color::White : fui::Color::Black;
        const int radius = std::min<int>(16, rect.height / 4);
        target.fill(rect, fui::Paint::solid(background), radius);
        if (command.style != 1) target.stroke(rect, fui::Paint::solid(foreground), 1, radius);
        auto style = screen.theme().bodyText;
        style.align = fui::TextAlign::Center;
        style.color = foreground;
        style.bold = command.style >= 2;
        if (command.style > 0 && std::strlen(label) <= 3 &&
            rect.height >= target.lineHeight(fui::GfxRendererTarget::FONT_LABEL) + 12)
          style.font = fui::GfxRendererTarget::FONT_LABEL;
        const bool left = !std::strcmp(label, "←"), right = !std::strcmp(label, "→");
        const bool up = !std::strcmp(label, "↑"), down = !std::strcmp(label, "↓");
        const bool erase = !std::strcmp(label, "⌫");
        if (left || right || up || down || erase) {
          // These symbols are absent from some built-in font subsets. Draw
          // bounded vectors so navigation never becomes a missing-glyph box.
          const int cx = rect.x + rect.width / 2, cy = rect.y + rect.height / 2;
          const int half = std::max(4, std::min<int>(rect.width, rect.height) / 5);
          const auto ink = fui::Paint::solid(foreground);
          auto line = [&](int x1, int y1, int x2, int y2) {
            target.line({static_cast<int16_t>(x1), static_cast<int16_t>(y1)},
                        {static_cast<int16_t>(x2), static_cast<int16_t>(y2)}, 2, ink);
          };
          if (erase) {
            line(cx - half, cy, cx - half / 2, cy - half / 2);
            line(cx - half, cy, cx - half / 2, cy + half / 2);
            line(cx - half / 2, cy - half / 2, cx + half, cy - half / 2);
            line(cx - half / 2, cy + half / 2, cx + half, cy + half / 2);
            line(cx + half, cy - half / 2, cx + half, cy + half / 2);
            line(cx, cy - half / 4, cx + half / 2, cy + half / 4);
            line(cx, cy + half / 4, cx + half / 2, cy - half / 4);
          } else if (left || right) {
            const int tip = cx + (left ? -half : half), back = tip + (left ? half : -half);
            line(cx - half, cy, cx + half, cy);
            line(tip, cy, back, cy - half);
            line(tip, cy, back, cy + half);
          } else {
            const int tip = cy + (up ? -half : half), back = tip + (up ? half : -half);
            line(cx, cy - half, cx, cy + half);
            line(cx, tip, cx - half, back);
            line(cx, tip, cx + half, back);
          }
        } else
          target.text(rect.inset({4, 6, 4, 6}), label, style);
        if (hasContentSelection() && nav.selected >= 0 && static_cast<size_t>(nav.selected) < canvas_->controlCount &&
            canvas_->controls[nav.selected] == i)
          target.stroke(rect.inset({3, 3, 3, 3}), fui::Paint::solid(foreground), 2, std::max(0, radius - 3));
        [[fallthrough]];
      }
      case crossstore::DrawKind::Hit:
        screen.frame().hit(rect, ACTION_USER, command.action, fui::InputTouch | fui::InputLongPress);
        break;
      case crossstore::DrawKind::Qr: {
        if (!qrReady_) break;
        constexpr int quiet = 4;
        const int modules = qrCode_.size + quiet * 2;
        const int scale = std::max(1, rect.width / modules);
        const int x = rect.x + (rect.width - modules * scale) / 2;
        const int y = rect.y + (rect.height - modules * scale) / 2;
        target.fill(rect, fui::Paint::solid(fui::Color::White));
        for (int moduleY = 0; moduleY < qrCode_.size; ++moduleY)
          for (int col = 0; col < qrCode_.size; ++col)
            if (qrcode_getModule(&qrCode_, col, moduleY))
              target.fill(
                  {static_cast<int16_t>(x + (col + quiet) * scale), static_cast<int16_t>(y + (moduleY + quiet) * scale),
                   static_cast<int16_t>(scale), static_cast<int16_t>(scale)},
                  fui::Paint::solid(fui::Color::Black));
        break;
      }
      case crossstore::DrawKind::Raster: {
        const int width = command.radius, height = command.font;
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width;) {
            const char value = label[y * width + x];
            int end = x + 1;
            while (end < width && label[y * width + end] == value) ++end;
            const auto color = shade(value <= '9' ? value - '0' : value - 'A' + 10);
            const int left = rect.x + x * rect.width / width;
            const int top = rect.y + y * rect.height / height;
            target.fill({static_cast<int16_t>(left), static_cast<int16_t>(top),
                         static_cast<int16_t>(rect.x + end * rect.width / width - left),
                         static_cast<int16_t>(rect.y + (y + 1) * rect.height / height - top)},
                        fui::Paint::solid(color));
            x = end;
          }
        }
        break;
      }
    }
  }
}

void LuaAppActivity::measure(const char* text, int maximum, unsigned font, int& width, int& height) {
  prepareUi();
  uiTarget.setFont(fui::GfxRendererTarget::FONT_LABEL, CONTROL_18_FONT_ID);
  fui::TextStyle style;
  switch (font) {
    case 0:
      style.font = fui::GfxRendererTarget::FONT_SMALL;
      break;
    case 1:
      style.font = fui::GfxRendererTarget::FONT_BODY;
      break;
    case 2:
      style.font = fui::GfxRendererTarget::FONT_TITLE;
      style.bold = true;
      break;
    case 3:
      style.font = fui::GfxRendererTarget::FONT_LABEL;
      style.bold = true;
      break;
  }
  style.maxLines = 12;
  const auto measured = fui::measureWrappedText(uiTarget, text, style, maximum);
  width = measured.width;
  height = measured.height;
}
