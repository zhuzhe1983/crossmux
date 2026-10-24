#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "components/Rect.h"

namespace crossstore {
enum class InstallMark : uint8_t { None, Installed, Update };

// Manifests already constrain each component to at most five decimal digits.
inline int compareVersions(const char* left, const char* right) {
  for (unsigned component = 0; component < 3; ++component) {
    uint32_t a = 0, b = 0;
    while (*left >= '0' && *left <= '9') a = a * 10 + static_cast<unsigned>(*left++ - '0');
    while (*right >= '0' && *right <= '9') b = b * 10 + static_cast<unsigned>(*right++ - '0');
    if (a != b) return a > b ? 1 : -1;
    if (*left == '.') ++left;
    if (*right == '.') ++right;
  }
  return 0;
}

inline InstallMark installMark(const char* installedVersion, const char* catalogVersion) {
  if (!installedVersion || !*installedVersion) return InstallMark::None;
  return catalogVersion && *catalogVersion && compareVersions(catalogVersion, installedVersion) > 0
             ? InstallMark::Update
             : InstallMark::Installed;
}

// One geometry source drives both drawing and touch. The limits also cap the
// live metadata/icon page at sixteen entries on every orientation and scale.
struct StoreGrid {
  static constexpr int GAP = 8;
  Rect all, installed, settings, status, body, previous, next, page;
  int columns = 1, rows = 1, capacity = 1;

  static int iconSize(const Rect& cell, int lineHeight, int levels) {
    const int limit = std::min(cell.width - 16, cell.height - lineHeight * 2 - 16);
    return levels >= 16 && limit >= 96 ? 96 : limit >= 64 ? 64 : 32;
  }

  static StoreGrid fit(Rect safe, int lineHeight, int smallHeight, int controlHeight, int sidePadding) {
    StoreGrid result;
    const int inset = std::min(std::max(GAP, sidePadding), safe.width / 8);
    safe.x += inset;
    safe.width = std::max(1, safe.width - inset * 2);
    const int head = std::max({44, controlHeight, lineHeight + 18});
    const int button = std::max(44, head);
    const int tabWidth = std::max(1, (safe.width - button - GAP * 2) / 2);
    result.all = Rect{safe.x, safe.y, tabWidth, head};
    result.installed = Rect{safe.x + tabWidth + GAP, safe.y, tabWidth, head};
    result.settings = Rect{safe.x + safe.width - button, safe.y, button, head};
    const int statusTop = safe.y + head + GAP;
    result.status = Rect{safe.x, statusTop, safe.width, smallHeight + 8};
    const int footer = std::max(44, smallHeight + 16);
    const int footerY = safe.y + safe.height - footer;
    const int side = std::min(std::max(64, lineHeight * 3), (safe.width - GAP * 2) / 3);
    result.previous = Rect{safe.x, footerY, side, footer};
    result.next = Rect{safe.x + safe.width - side, footerY, side, footer};
    result.page = Rect{safe.x + side + GAP, footerY, safe.width - side * 2 - GAP * 2, footer};
    const int bodyTop = statusTop + result.status.height + GAP;
    result.body = Rect{safe.x, bodyTop, safe.width, std::max(1, footerY - GAP - bodyTop)};
    const int minWidth = std::max(96, lineHeight * 4);
    const int minHeight = std::max(108, lineHeight * 2 + 76);
    result.columns = std::clamp((result.body.width + GAP) / (minWidth + GAP), 1, 4);
    result.rows = std::clamp((result.body.height + GAP) / (minHeight + GAP), 1, 4);
    result.capacity = result.columns * result.rows;
    return result;
  }

  Rect cell(size_t index) const {
    const int col = static_cast<int>(index) % columns;
    const int row = static_cast<int>(index) / columns;
    const int width = (body.width - (columns - 1) * GAP) / columns;
    const int height = (body.height - (rows - 1) * GAP) / rows;
    return Rect{body.x + col * (width + GAP), body.y + row * (height + GAP), width, height};
  }
};
}  // namespace crossstore
