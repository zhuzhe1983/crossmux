#include "PackageIcons.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "IconBundle.h"
#include "Package.h"

namespace crossstore {
namespace {
bool nearSegment(int x, int y, int ax, int ay, int bx, int by, int radius) {
  const int vx = bx - ax, vy = by - ay;
  const int px = x - ax, py = y - ay;
  const int dot = px * vx + py * vy, length = vx * vx + vy * vy;
  if (dot <= 0) return px * px + py * py <= radius * radius;
  if (dot >= length) return (x - bx) * (x - bx) + (y - by) * (y - by) <= radius * radius;
  const int cross = px * vy - py * vx;
  return cross * cross <= radius * radius * length;
}

bool markSymbol(InstallMark mark, int x, int y, int diameter) {
  const int quarter = diameter / 4, middle = diameter / 2, end = diameter * 3 / 4;
  const int radius = std::max(1, diameter / 12);
  switch (mark) {
    case InstallMark::None:
      return false;
    case InstallMark::Installed:
      return nearSegment(x, y, quarter, middle, diameter * 5 / 12, diameter * 2 / 3, radius) ||
             nearSegment(x, y, diameter * 5 / 12, diameter * 2 / 3, end, diameter / 3, radius);
    case InstallMark::Update:
      return nearSegment(x, y, middle, end, middle, quarter, radius) ||
             nearSegment(x, y, quarter, middle, middle, quarter, radius) ||
             nearSegment(x, y, middle, quarter, end, middle, radius);
  }
  return false;
}

memory::ByteBuffer iconBuffer(const IconVariant& variant) {
  // A visible icon survives callbacks and cannot live on the stack. The
  // largest block is 4608 bytes; 16-gray buffers require PSRAM rather than
  // falling back into the C3-sized internal heap. Low-gray buffers are <=1 KiB.
  if (memory::psramHasHeadroom(variant.bytes, variant.bytes, 0)) {
    auto result = memory::makePsramByteBufferUninitializedNoThrow(variant.bytes);
    if (result) return result;
  }
  if (variant.levels < 16) return memory::makeInternalByteBufferNoThrow(variant.bytes);
  return {};
}
}  // namespace

void PackageIcons::clear() {
  for (size_t slot = 0; slot < icons_.size(); ++slot) clear(slot);
  paintedCount_ = 0;
}

void PackageIcons::clear(size_t slot) {
  if (slot < icons_.size()) icons_[slot] = {};
}

bool PackageIcons::loadPackage(size_t slot, const char* path, int levels, int desiredSize) {
  if (slot >= icons_.size()) return false;
  clear(slot);
  if (!path || !*path) return false;
  const auto& variant = iconVariant(levels, desiredSize);
  char entry[40];
  std::snprintf(entry, sizeof(entry), "assets/icon-g%u-%u.bin", variant.levels, variant.size);
  size_t bytes = 0;
  auto raw = readEntry(path, entry, variant.bytes, bytes);
  if (!raw || bytes != variant.bytes) return false;
  auto& icon = icons_[slot];
  if (variant.levels == 16 || memory::psramHasHeadroom(variant.bytes, variant.bytes, 0)) {
    icon.pixels = iconBuffer(variant);
    if (icon.pixels) std::memcpy(icon.pixels.get(), raw.get(), variant.bytes);
  } else {
    icon.pixels = std::move(raw);
  }
  if (!icon.pixels && variant.levels < 16) icon.pixels = std::move(raw);
  if (!icon.pixels) {
    LOG_ERR("STORE", "Cannot allocate icon: %u bytes", variant.bytes);
    return false;
  }
  icon.size = variant.size;
  icon.bits = variant.bits;
  return true;
}

bool PackageIcons::loadBundle(size_t slot, const char* path, int levels, int desiredSize) {
  if (slot >= icons_.size()) return false;
  clear(slot);
  if (!path || !*path) return false;
  auto file = Storage.open(path);
  uint8_t header[sizeof(ICON_BUNDLE_HEADER)];
  if (!file || file.isDirectory() || file.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) ||
      !validIconBundleHeader(header, sizeof(header), file.size()))
    return false;
  const auto& variant = iconVariant(levels, desiredSize);
  auto pixels = iconBuffer(variant);
  if (!pixels) {
    LOG_ERR("STORE", "Cannot allocate icon: %u bytes", variant.bytes);
    return false;
  }
  if (!file.seek(variant.offset) || file.read(pixels.get(), variant.bytes) != variant.bytes) return false;
  auto& icon = icons_[slot];
  icon.pixels = std::move(pixels);
  icon.size = variant.size;
  icon.bits = variant.bits;
  return true;
}

bool PackageIcons::loadEmbeddedBundle(size_t slot, const uint8_t* bytes, size_t length, int levels, int desiredSize) {
  if (slot >= icons_.size()) return false;
  clear(slot);
  if (!validIconBundleHeader(bytes, sizeof(ICON_BUNDLE_HEADER), length)) return false;
  const auto& variant = iconVariant(levels, desiredSize);
  auto& icon = icons_[slot];
  icon.borrowed = bytes + variant.offset;
  icon.size = variant.size;
  icon.bits = variant.bits;
  return true;
}

void PackageIcons::draw(GfxRenderer& renderer, int slot, int x, int y, int size, int levels) {
  if (size <= 0 || !renderer.glyphIntersectsStrip(x, y, x + size, y + size)) return;
  if (slot < 0 || static_cast<size_t>(slot) >= icons_.size() || !icons_[slot].data()) {
    if (renderer.getRenderMode() != GfxRenderer::BW) return;
    const int unit = size / 4;
    for (int r = 0; r < 2; ++r)
      for (int c = 0; c < 2; ++c)
        renderer.fillRect(x + unit / 2 + c * (unit + unit / 2), y + unit / 2 + r * (unit + unit / 2), unit, unit, true);
    return;
  }
  const auto& icon = icons_[slot];
  const auto* pixels = icon.data();
  const unsigned mask = (1u << icon.bits) - 1;
  for (int dx = 0; dx < size; ++dx) {
    if (!renderer.glyphIntersectsStrip(x + dx, y, x + dx + 1, y + size)) continue;
    for (int dy = 0; dy < size; ++dy) {
      if (!renderer.glyphIntersectsStrip(x + dx, y + dy, x + dx + 1, y + dy + 1)) continue;
      const unsigned bit = ((dy * icon.size / size) * icon.size + dx * icon.size / size) * icon.bits;
      const unsigned value = (pixels[bit / 8] >> (8 - icon.bits - bit % 8)) & mask;
      if (levels == 16 && renderer.isGrayscale16Active())
        renderer.drawGrayscale16Pixel(x + dx, y + dy, value * 255 / mask);
      else if (renderer.getRenderMode() == GfxRenderer::BW)
        renderer.drawPixel(x + dx, y + dy, value != mask);
      else {
        const auto pixel = GfxRenderer::mapTwoBitPixel(renderer.getRenderMode(), value * 3 / mask);
        if (pixel.draw) renderer.drawPixel(x + dx, y + dy, pixel.state);
      }
    }
  }
}

void PackageIcons::drawMark(GfxRenderer& renderer, int x, int y, int size, InstallMark mark) {
  if (mark == InstallMark::None || size < 16) return;
  const int diameter = size >= 96 ? 26 : size >= 64 ? 22 : 16;
  const int left = x + size - diameter, top = y + size - diameter;
  if (!renderer.glyphIntersectsStrip(left, top, left + diameter, top + diameter)) return;
  const int outer = diameter - 1, inner = diameter - 4;
  for (int dx = 0; dx < diameter; ++dx) {
    if (!renderer.glyphIntersectsStrip(left + dx, top, left + dx + 1, top + diameter)) continue;
    for (int dy = 0; dy < diameter; ++dy) {
      if (!renderer.glyphIntersectsStrip(left + dx, top + dy, left + dx + 1, top + dy + 1)) continue;
      const int rx = dx * 2 - outer, ry = dy * 2 - outer;
      const int distance = rx * rx + ry * ry;
      if (distance > outer * outer) continue;
      if (renderer.getRenderMode() != GfxRenderer::BW) {
        // Overlay planes contain gray modulation, not absolute black/white.
        // Restore neutral (zero) bits over the entire badge so the completed
        // B/W base's white disc and black symbol survive the icon gray pass.
        renderer.drawPixel(left + dx, top + dy, true);
        continue;
      }
      const bool black = distance >= inner * inner || markSymbol(mark, dx, dy, diameter);
      if (renderer.isGrayscale16Active())
        renderer.drawGrayscale16Pixel(left + dx, top + dy, black ? 0 : 255);
      else
        renderer.drawPixel(left + dx, top + dy, black);
    }
  }
}

void PackageIcons::beginFrame(GfxRenderer& renderer) {
  paintedCount_ = 0;
  output_ = Output::Monochrome;
  renderer.setRenderMode(GfxRenderer::BW);
  if (renderer.isInverted()) return;
  if (renderer.getGrayscaleLevels() == 16) {
    if (renderer.beginGrayscale16()) output_ = Output::Native;
    return;
  }
  if (renderer.getGrayscaleLevels() >= 4 && renderer.grayscaleCapabilities().stripUploads &&
      renderer.getDisplayWidthBytes() <= 256)
    output_ = Output::Overlay;
}

void PackageIcons::paint(GfxRenderer& renderer, int slot, int x, int y, int size, InstallMark mark) {
  int levels = 2;
  switch (output_) {
    case Output::Monochrome:
      break;
    case Output::Overlay:
      levels = 4;
      break;
    case Output::Native:
      levels = 16;
      break;
  }
  draw(renderer, slot, x, y, size, levels);
  drawMark(renderer, x, y, size, mark);
  if (slot < 0 || static_cast<size_t>(slot) >= icons_.size() || !icons_[slot].data() ||
      paintedCount_ == painted_.size())
    return;
  const auto clip = renderer.getClipRect();
  painted_[paintedCount_++] = {static_cast<int16_t>(x),
                               static_cast<int16_t>(y),
                               static_cast<int16_t>(size),
                               static_cast<uint8_t>(slot),
                               mark,
                               {static_cast<int16_t>(clip[0]), static_cast<int16_t>(clip[1]),
                                static_cast<int16_t>(clip[2]), static_cast<int16_t>(clip[3])}};
}

void PackageIcons::displayFrame(GfxRenderer& renderer) {
  switch (output_) {
    case Output::Monochrome:
      renderer.displayBuffer();
      return;
    case Output::Native:
      if (!renderer.commitGrayscale16()) renderer.displayBuffer();
      return;
    case Output::Overlay:
      break;
  }
  renderer.displayGrayscaleBase();
  // Reuse the existing strip-only icon pass; no second screen-sized buffer.
  uint8_t scratch[256];
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.setRenderMode(mode);
    for (int y = 0; y < renderer.getDisplayHeight(); ++y) {
      renderer.beginStripTarget(scratch, y, 1);
      renderer.clearScreen(0);
      for (size_t i = 0; i < paintedCount_; ++i) {
        const auto& icon = painted_[i];
        const GfxRenderer::ClipScope clip(renderer, icon.clip[0], icon.clip[1], icon.clip[2], icon.clip[3]);
        draw(renderer, icon.slot, icon.x, icon.y, icon.size, 4);
        drawMark(renderer, icon.x, icon.y, icon.size, icon.mark);
      }
      renderer.endStripTarget();
      renderer.writeGrayscalePlaneStrip(mode == GfxRenderer::GRAYSCALE_LSB, scratch, y, 1);
    }
  }
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.cleanupGrayscaleWithFrameBuffer();
}
}  // namespace crossstore
