#pragma once

#include <algorithm>

#include "components/Rect.h"

namespace crossstore {
struct StoreConfirmation {
  static constexpr int GAP = 8;
  Rect list, buttons[3];

  static StoreConfirmation fit(Rect content, int lineHeight, int touchMinimum, int sidePadding, int count) {
    StoreConfirmation result{content, {Rect{}, Rect{}, Rect{}}};
    count = std::clamp(count, 1, 3);
    const int side = std::min(std::max(GAP, sidePadding), content.width / 8);
    const int minimum = std::max(44, touchMinimum);
    const int desiredHeight = std::max(minimum, lineHeight * 2 + 16);
    const int info = std::max(minimum, lineHeight * 2 + 16);
    // Landscape keeps room for at least one complete explanatory row. Two
    // adjacent actions share the top button row; Cancel has its own full row
    // for the three-choice removal screen.
    const bool compact = count > 1 && content.height < info + count * (desiredHeight + GAP) + GAP &&
                         content.width - side * 2 >= minimum * 2 + GAP;
    const int rows = compact ? count - 1 : count;
    const int available = (content.height - info - GAP * (rows + 1)) / rows;
    const int height = std::max(minimum, std::min(lineHeight * 2 + 16, available));
    const int top = content.y + content.height - GAP - rows * height - (rows - 1) * GAP;
    result.list.height = std::max(1, top - GAP - content.y);
    for (int i = 0; i < count; ++i) {
      const int width = content.width - side * 2;
      if (compact && i < 2)
        result.buttons[i] = Rect{content.x + side + i * ((width - GAP) / 2 + GAP), top, (width - GAP) / 2, height};
      else
        result.buttons[i] = Rect{content.x + side, top + (i - (compact ? 1 : 0)) * (height + GAP), width, height};
    }
    return result;
  }
};
}  // namespace crossstore
