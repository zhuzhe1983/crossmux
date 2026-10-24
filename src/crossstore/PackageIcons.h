#pragma once

#include <Memory.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "StoreGrid.h"

class GfxRenderer;

namespace crossstore {
// The owner loads one visible page on its loop task under RenderLock. Rendering
// uses only these bounded bytes; no filesystem or per-frame icon allocation.
class PackageIcons {
 public:
  static constexpr size_t CAPACITY = 16;
  void clear();
  void clear(size_t slot);
  // A small cell uses its preprocessed 32px asset; downsampling a dithered
  // 64px source would discard the pattern. Zero keeps the large variant.
  bool loadPackage(size_t slot, const char* path, int levels, int desiredSize = 0);
  bool loadBundle(size_t slot, const char* path, int levels, int desiredSize = 0);
  // Borrow a fixed bundle from memory-mapped Flash without allocation or I/O.
  // The caller must keep these bytes immutable and alive until clear/replace;
  // use static firmware assets, never a temporary download or stack buffer.
  bool loadEmbeddedBundle(size_t slot, const uint8_t* bytes, size_t length, int levels, int desiredSize = 0);

  // Call at the start of each UI build: scanning or viewport layout may build
  // several times before the final gray-plane pass.
  void resetPlacements() { paintedCount_ = 0; }
  void beginFrame(GfxRenderer& renderer);
  void paint(GfxRenderer& renderer, int slot, int x, int y, int size, InstallMark mark = InstallMark::None);
  void displayFrame(GfxRenderer& renderer);

 private:
  enum class Output : uint8_t { Monochrome, Overlay, Native };
  struct CachedIcon {
    memory::ByteBuffer pixels;
    const uint8_t* borrowed = nullptr;
    uint8_t size = 0, bits = 0;
    const uint8_t* data() const { return pixels ? pixels.get() : borrowed; }
  };
  struct Placement {
    int16_t x, y, size;
    uint8_t slot;
    InstallMark mark;
    std::array<int16_t, 4> clip;
  };
  // At most 72 KiB for 16-gray (PSRAM) or 16 KiB for 4-gray. Individual
  // allocations avoid a page-sized contiguous block; released on page/exit.
  std::array<CachedIcon, CAPACITY> icons_{};
  std::array<Placement, CAPACITY> painted_{};
  size_t paintedCount_ = 0;
  Output output_ = Output::Monochrome;
  void draw(GfxRenderer& renderer, int slot, int x, int y, int size, int levels);
  void drawMark(GfxRenderer& renderer, int x, int y, int size, InstallMark mark);
};
}  // namespace crossstore
