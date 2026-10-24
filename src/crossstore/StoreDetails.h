#pragma once

#include <algorithm>

#include "components/Rect.h"

namespace crossstore {
// Derived from the same safe content band used by the details list. Keeping
// this pure geometry shared makes the painted button and its hit area agree.
struct StoreDetails {
  static constexpr int GAP = 8;
  Rect list, primary;

  static StoreDetails fit(Rect content, int lineHeight, int touchMinimum, int sidePadding, bool hasPrimary) {
    StoreDetails layout{content, Rect{}};
    if (!hasPrimary) return layout;
    const int side = std::min(std::max(GAP, sidePadding), content.width / 8);
    const int height = std::max({44, touchMinimum, lineHeight * 2 + 16});
    layout.primary =
        Rect{content.x + side, content.y + content.height - GAP - height, content.width - side * 2, height};
    layout.list.height = std::max(1, layout.primary.y - GAP - content.y);
    return layout;
  }
};
}  // namespace crossstore
