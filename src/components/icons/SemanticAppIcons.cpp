#include "SemanticAppIcons.h"

#include <BoardConfig.h>

#include "../../../assets/app-icons/semantic-v1/g16/32/plain/icons.h"
#include "../../../assets/app-icons/semantic-v1/g16/96/plain/icons.h"
#include "../../../assets/app-icons/semantic-v1/g2/32/plain/icons.h"
#include "../../../assets/app-icons/semantic-v1/g2/64/dither/icons.h"
#include "../../../assets/app-icons/semantic-v1/g4/32/plain/icons.h"
#include "../../../assets/app-icons/semantic-v1/g4/64/dither/icons.h"

namespace {
struct PackedIcon {
  const uint8_t* data;
  unsigned size;
  unsigned bits;
};
PackedIcon icon2_32(const UIIcon icon) {
  using namespace crossmux_icons_g2_32_plain;
  switch (icon) {
    case UIIcon::Library:
      return {library, width, bitsPerPixel};
    case UIIcon::Transfer:
      return {transfer, width, bitsPerPixel};
    case UIIcon::Opds:
      return {opds, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::WeRead:
      return {weread, width, bitsPerPixel};
#endif
    case UIIcon::AirPage:
      return {airpage, width, bitsPerPixel};
    case UIIcon::ReadingStats:
      return {reading_stats, width, bitsPerPixel};
    case UIIcon::Sudoku:
      return {sudoku, width, bitsPerPixel};
    case UIIcon::Gomoku:
      return {gomoku, width, bitsPerPixel};
    case UIIcon::Sokoban:
      return {sokoban, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::ChineseChess:
      return {chinese_chess, width, bitsPerPixel};
#endif
    case UIIcon::Minesweeper:
      return {minesweeper, width, bitsPerPixel};
    case UIIcon::Game2048:
      return {game2048, width, bitsPerPixel};
    case UIIcon::Avatar:
      return {avatar, width, bitsPerPixel};
    case UIIcon::Buddy:
      return {buddy, width, bitsPerPixel};
    case UIIcon::PixelSwitch:
      return {pixel_switch, width, bitsPerPixel};
    case UIIcon::Calculator:
      return {calculator, width, bitsPerPixel};
    case UIIcon::Woodfish:
      return {woodfish, width, bitsPerPixel};
    case UIIcon::Standby:
      return {standby, width, bitsPerPixel};
    default:
      return {nullptr, 0, 0};  // Non-app chrome has separate assets.
  }
}
PackedIcon icon2_64(const UIIcon icon) {
  using namespace crossmux_icons_g2_64_dither;
  switch (icon) {
    case UIIcon::Library:
      return {library, width, bitsPerPixel};
    case UIIcon::Transfer:
      return {transfer, width, bitsPerPixel};
    case UIIcon::Opds:
      return {opds, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::WeRead:
      return {weread, width, bitsPerPixel};
#endif
    case UIIcon::AirPage:
      return {airpage, width, bitsPerPixel};
    case UIIcon::ReadingStats:
      return {reading_stats, width, bitsPerPixel};
    case UIIcon::Sudoku:
      return {sudoku, width, bitsPerPixel};
    case UIIcon::Gomoku:
      return {gomoku, width, bitsPerPixel};
    case UIIcon::Sokoban:
      return {sokoban, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::ChineseChess:
      return {chinese_chess, width, bitsPerPixel};
#endif
    case UIIcon::Minesweeper:
      return {minesweeper, width, bitsPerPixel};
    case UIIcon::Game2048:
      return {game2048, width, bitsPerPixel};
    case UIIcon::Avatar:
      return {avatar, width, bitsPerPixel};
    case UIIcon::Buddy:
      return {buddy, width, bitsPerPixel};
    case UIIcon::PixelSwitch:
      return {pixel_switch, width, bitsPerPixel};
    case UIIcon::Calculator:
      return {calculator, width, bitsPerPixel};
    case UIIcon::Woodfish:
      return {woodfish, width, bitsPerPixel};
    case UIIcon::Standby:
      return {standby, width, bitsPerPixel};
    default:
      return {nullptr, 0, 0};  // Non-app chrome has separate assets.
  }
}
PackedIcon icon4_32(const UIIcon icon) {
  using namespace crossmux_icons_g4_32_plain;
  switch (icon) {
    case UIIcon::Library:
      return {library, width, bitsPerPixel};
    case UIIcon::Transfer:
      return {transfer, width, bitsPerPixel};
    case UIIcon::Opds:
      return {opds, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::WeRead:
      return {weread, width, bitsPerPixel};
#endif
    case UIIcon::AirPage:
      return {airpage, width, bitsPerPixel};
    case UIIcon::ReadingStats:
      return {reading_stats, width, bitsPerPixel};
    case UIIcon::Sudoku:
      return {sudoku, width, bitsPerPixel};
    case UIIcon::Gomoku:
      return {gomoku, width, bitsPerPixel};
    case UIIcon::Sokoban:
      return {sokoban, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::ChineseChess:
      return {chinese_chess, width, bitsPerPixel};
#endif
    case UIIcon::Minesweeper:
      return {minesweeper, width, bitsPerPixel};
    case UIIcon::Game2048:
      return {game2048, width, bitsPerPixel};
    case UIIcon::Avatar:
      return {avatar, width, bitsPerPixel};
    case UIIcon::Buddy:
      return {buddy, width, bitsPerPixel};
    case UIIcon::PixelSwitch:
      return {pixel_switch, width, bitsPerPixel};
    case UIIcon::Calculator:
      return {calculator, width, bitsPerPixel};
    case UIIcon::Woodfish:
      return {woodfish, width, bitsPerPixel};
    case UIIcon::Standby:
      return {standby, width, bitsPerPixel};
    default:
      return {nullptr, 0, 0};  // Non-app chrome has separate assets.
  }
}
PackedIcon icon4_64(const UIIcon icon) {
  using namespace crossmux_icons_g4_64_dither;
  switch (icon) {
    case UIIcon::Library:
      return {library, width, bitsPerPixel};
    case UIIcon::Transfer:
      return {transfer, width, bitsPerPixel};
    case UIIcon::Opds:
      return {opds, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::WeRead:
      return {weread, width, bitsPerPixel};
#endif
    case UIIcon::AirPage:
      return {airpage, width, bitsPerPixel};
    case UIIcon::ReadingStats:
      return {reading_stats, width, bitsPerPixel};
    case UIIcon::Sudoku:
      return {sudoku, width, bitsPerPixel};
    case UIIcon::Gomoku:
      return {gomoku, width, bitsPerPixel};
    case UIIcon::Sokoban:
      return {sokoban, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::ChineseChess:
      return {chinese_chess, width, bitsPerPixel};
#endif
    case UIIcon::Minesweeper:
      return {minesweeper, width, bitsPerPixel};
    case UIIcon::Game2048:
      return {game2048, width, bitsPerPixel};
    case UIIcon::Avatar:
      return {avatar, width, bitsPerPixel};
    case UIIcon::Buddy:
      return {buddy, width, bitsPerPixel};
    case UIIcon::PixelSwitch:
      return {pixel_switch, width, bitsPerPixel};
    case UIIcon::Calculator:
      return {calculator, width, bitsPerPixel};
    case UIIcon::Woodfish:
      return {woodfish, width, bitsPerPixel};
    case UIIcon::Standby:
      return {standby, width, bitsPerPixel};
    default:
      return {nullptr, 0, 0};  // Non-app chrome has separate assets.
  }
}
PackedIcon icon16_32(const UIIcon icon) {
  using namespace crossmux_icons_g16_32_plain;
  switch (icon) {
    case UIIcon::Library:
      return {library, width, bitsPerPixel};
    case UIIcon::Transfer:
      return {transfer, width, bitsPerPixel};
    case UIIcon::Opds:
      return {opds, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::WeRead:
      return {weread, width, bitsPerPixel};
#endif
    case UIIcon::AirPage:
      return {airpage, width, bitsPerPixel};
    case UIIcon::ReadingStats:
      return {reading_stats, width, bitsPerPixel};
    case UIIcon::Sudoku:
      return {sudoku, width, bitsPerPixel};
    case UIIcon::Gomoku:
      return {gomoku, width, bitsPerPixel};
    case UIIcon::Sokoban:
      return {sokoban, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::ChineseChess:
      return {chinese_chess, width, bitsPerPixel};
#endif
    case UIIcon::Minesweeper:
      return {minesweeper, width, bitsPerPixel};
    case UIIcon::Game2048:
      return {game2048, width, bitsPerPixel};
    case UIIcon::Avatar:
      return {avatar, width, bitsPerPixel};
    case UIIcon::Buddy:
      return {buddy, width, bitsPerPixel};
    case UIIcon::PixelSwitch:
      return {pixel_switch, width, bitsPerPixel};
    case UIIcon::Calculator:
      return {calculator, width, bitsPerPixel};
    case UIIcon::Woodfish:
      return {woodfish, width, bitsPerPixel};
    case UIIcon::Standby:
      return {standby, width, bitsPerPixel};
    default:
      return {nullptr, 0, 0};  // Non-app chrome has separate assets.
  }
}
PackedIcon icon16_96(const UIIcon icon) {
  using namespace crossmux_icons_g16_96_plain;
  switch (icon) {
    case UIIcon::Library:
      return {library, width, bitsPerPixel};
    case UIIcon::Transfer:
      return {transfer, width, bitsPerPixel};
    case UIIcon::Opds:
      return {opds, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::WeRead:
      return {weread, width, bitsPerPixel};
#endif
    case UIIcon::AirPage:
      return {airpage, width, bitsPerPixel};
    case UIIcon::ReadingStats:
      return {reading_stats, width, bitsPerPixel};
    case UIIcon::Sudoku:
      return {sudoku, width, bitsPerPixel};
    case UIIcon::Gomoku:
      return {gomoku, width, bitsPerPixel};
    case UIIcon::Sokoban:
      return {sokoban, width, bitsPerPixel};
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::ChineseChess:
      return {chinese_chess, width, bitsPerPixel};
#endif
    case UIIcon::Minesweeper:
      return {minesweeper, width, bitsPerPixel};
    case UIIcon::Game2048:
      return {game2048, width, bitsPerPixel};
    case UIIcon::Avatar:
      return {avatar, width, bitsPerPixel};
    case UIIcon::Buddy:
      return {buddy, width, bitsPerPixel};
    case UIIcon::PixelSwitch:
      return {pixel_switch, width, bitsPerPixel};
    case UIIcon::Calculator:
      return {calculator, width, bitsPerPixel};
    case UIIcon::Woodfish:
      return {woodfish, width, bitsPerPixel};
    case UIIcon::Standby:
      return {standby, width, bitsPerPixel};
    default:
      return {nullptr, 0, 0};  // Non-app chrome has separate assets.
  }
}

PackedIcon selectIcon(const UIIcon icon, const int size, const int levels) {
  // Eliminate native assets entirely from C3 and four-level firmware images.
  if constexpr (BoardConfig::DEFAULT_DEVICE.grayscaleLevels == 16) {
    if (levels == 16) return size <= 32 ? icon16_32(icon) : icon16_96(icon);
  }
  if (levels == 4) return size <= 32 ? icon4_32(icon) : icon4_64(icon);
  return size <= 32 ? icon2_32(icon) : icon2_64(icon);
}
}  // namespace

void SemanticAppIcons::draw(const GfxRenderer& renderer, const UIIcon icon, const int x, const int y, const int size,
                            const int levels, const bool inverted) {
  if (size <= 0 || !renderer.glyphIntersectsStrip(x, y, x + size, y + size)) return;
  const auto source = selectIcon(icon, size, levels);
  if (!source.data) return;
  const unsigned mask = (1u << source.bits) - 1;
  const unsigned stride = source.size * source.bits / 8;
  for (int dx = 0; dx < size; ++dx) {
    // Portrait panels map logical columns onto physical strip rows.
    if (!renderer.glyphIntersectsStrip(x + dx, y, x + dx + 1, y + size)) continue;
    const unsigned sx = dx * source.size / size;
    for (int dy = 0; dy < size; ++dy) {
      if (!renderer.glyphIntersectsStrip(x + dx, y + dy, x + dx + 1, y + dy + 1)) continue;
      const unsigned sy = dy * source.size / size;
      const unsigned bit = sx * source.bits;
      unsigned value = (source.data[sy * stride + bit / 8] >> (8 - source.bits - bit % 8)) & mask;
      if (inverted) value = mask - value;
      if (renderer.isGrayscale16Active()) {
        renderer.drawGrayscale16Pixel(x + dx, y + dy, value * 255 / mask);
      } else if (renderer.getRenderMode() == GfxRenderer::BW) {
        // Overlay gray pixels start black, like the renderer's image pipeline.
        renderer.drawPixel(x + dx, y + dy, value != mask);
      } else {
        const auto pixel = GfxRenderer::mapTwoBitPixel(renderer.getRenderMode(), value * 3 / mask);
        if (pixel.draw) renderer.drawPixel(x + dx, y + dy, pixel.state);
      }
    }
  }
}
