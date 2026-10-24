#include "LuaWeReadCovers.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t DITHER[] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
uint32_t little32(const uint8_t* bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
}  // namespace

bool LuaWeReadCovers::parsePlacement(const char* argument, int (&values)[4]) {
  if (!argument) return false;
  for (size_t part = 0; part < 4; ++part) {
    int value = 0;
    if (*argument < '0' || *argument > '9') return false;
    do {
      const unsigned digit = *argument++ - '0';
      if (value > (INT16_MAX - static_cast<int>(digit)) / 10) return false;
      value = value * 10 + digit;
    } while (*argument >= '0' && *argument <= '9');
    values[part] = value;
    if (part < 3) {
      if (*argument++ != ':') return false;
    } else if (*argument) {
      return false;
    }
  }
  return values[2] > 0 && values[3] > 0;
}

void LuaWeReadCovers::clear() {
  file_.close();
  std::generate(slots_.begin(), slots_.end(), [] { return Slot{}; });
  count_ = 0;
}

bool LuaWeReadCovers::place(uint32_t index, int x, int y, int width, int height) {
  if (!index || count_ == slots_.size() || x < 0 || y < 0 || width <= 0 || height <= 0 || x > INT16_MAX ||
      y > INT16_MAX || width > INT16_MAX - x || height > INT16_MAX - y)
    return false;
  auto& slot = slots_[count_++];
  slot.requested = index;
  slot.x = x;
  slot.y = y;
  slot.width = width;
  slot.height = height;
  return true;
}

void LuaWeReadCovers::prepare() {
  for (size_t i = 0; i < slots_.size(); ++i) {
    auto& slot = slots_[i];
    if (i >= count_) {
      slot = {};
    } else if (slot.requested != slot.loaded) {
      slot.pixels.reset();
      slot.loaded = slot.requested;
      slot.state = State::Empty;
      slot.sourceWidth = slot.sourceHeight = 0;
    }
  }
}

void LuaWeReadCovers::unavailable(size_t slot) {
  if (slot < count_) slots_[slot].state = State::Unavailable;
}

LuaWeReadCovers::State LuaWeReadCovers::load(size_t position, const char* path) {
  if (position >= count_) return State::Invalid;
  auto& slot = slots_[position];
  slot.pixels.reset();
  slot.state = State::Invalid;
  file_.close();
  // Member handle is closed on every exit; Bitmap only borrows it while loading.
  struct CloseFile {
    HalFile& file;
    ~CloseFile() { file.close(); }
  } close{file_};
  if (!path || !*path) return slot.state;
  if (!Storage.exists(path)) return slot.state = State::Missing;
  if (!Storage.openFileForRead("WR", path, file_) || file_.isDirectory()) return slot.state;
  // Reject corrupt/extreme dimensions before Bitmap's signed-height conversion.
  uint8_t header[26];
  if (file_.read(header, sizeof(header)) != sizeof(header) || header[0] != 'B' || header[1] != 'M') return slot.state;
  const uint32_t width = little32(header + 18), height = little32(header + 22);
  if (!width || width > MAX_WIDTH || !height || (height > MAX_HEIGHT && height < UINT32_MAX - MAX_HEIGHT + 1))
    return slot.state;
  if (bitmap_.parseHeaders() != BmpReaderError::Ok || bitmap_.getRowBytes() > static_cast<int>(sizeof(row_)) ||
      bitmap_.hasTransparency() ||
      uint64_t(bitmap_.getPixelDataOffset()) + uint64_t(bitmap_.getRowBytes()) * bitmap_.getHeight() > file_.size())
    return slot.state;
  const size_t bytes = (size_t(bitmap_.getWidth()) * bitmap_.getHeight() + 1) / 2;
  if (!bytes || bytes > MAX_BYTES) return slot.state;
  // Cached pixels outlive the callback. Each visible thumbnail needs <=9184 B,
  // at most 110208 B for 12; PSRAM only, never a second screen/internal fallback.
  slot.pixels = HalMemory::allocatePsram(bytes);
  if (!slot.pixels) {
    LOG_ERR("WR", "No PSRAM for cover thumbnail (%u bytes)", static_cast<unsigned>(bytes));
    return slot.state = State::NoMemory;
  }
  std::memset(slot.pixels.get(), 0, bytes);
  for (int row = 0; row < bitmap_.getHeight(); ++row) {
    if (bitmap_.readNextRow(gray_, row_, nullptr, Bitmap::RowOutput::Gray8) != BmpReaderError::Ok) {
      slot.pixels.reset();
      return slot.state;
    }
    const int y = bitmap_.isTopDown() ? row : bitmap_.getHeight() - 1 - row;
    for (int x = 0; x < bitmap_.getWidth(); ++x) {
      const size_t pixel = size_t(y) * bitmap_.getWidth() + x;
      const uint8_t shade = (unsigned(gray_[x]) * 15 + 127) / 255;
      slot.pixels[pixel / 2] |= shade << (pixel % 2 ? 0 : 4);
    }
  }
  slot.sourceWidth = bitmap_.getWidth();
  slot.sourceHeight = bitmap_.getHeight();
  return slot.state = State::Ready;
}

void LuaWeReadCovers::paint(const GfxRenderer& renderer, int levels) const {
  for (size_t i = 0; i < count_; ++i) {
    const auto& slot = slots_[i];
    if (slot.state != State::Ready || slot.loaded != slot.requested || !slot.pixels) continue;
    const int width =
        std::min<int>(slot.sourceWidth, std::min<int>(slot.width, slot.height * slot.sourceWidth / slot.sourceHeight));
    const int height = std::max(1, width * slot.sourceHeight / slot.sourceWidth);
    if (width <= 0) continue;
    const int x = slot.x + (slot.width - width) / 2, y = slot.y + (slot.height - height) / 2;
    if (!renderer.glyphIntersectsStrip(x, y, x + width, y + height)) continue;
    for (int dy = 0; dy < height; ++dy) {
      for (int dx = 0; dx < width; ++dx) {
        if (!renderer.glyphIntersectsStrip(x + dx, y + dy, x + dx + 1, y + dy + 1)) continue;
        const size_t pixel = (dy * slot.sourceHeight / height) * slot.sourceWidth + dx * slot.sourceWidth / width;
        const unsigned shade = (slot.pixels[pixel / 2] >> (pixel % 2 ? 0 : 4)) & 15;
        if (levels == 16 && renderer.isGrayscale16Active()) {
          renderer.drawGrayscale16Pixel(x + dx, y + dy, shade * 17);
        } else if (levels == 2) {
          renderer.drawPixel(x + dx, y + dy, shade < 15 && shade <= DITHER[(dy & 3) * 4 + (dx & 3)]);
        } else {
          const unsigned value = (shade * 3 + 7) / 15;
          if (renderer.getRenderMode() == GfxRenderer::BW)
            renderer.drawPixel(x + dx, y + dy, value != 3);
          else {
            const auto mapped = GfxRenderer::mapTwoBitPixel(renderer.getRenderMode(), value);
            if (mapped.draw) renderer.drawPixel(x + dx, y + dy, mapped.state);
          }
        }
      }
    }
  }
}

bool LuaWeReadCovers::beginFrame(GfxRenderer& renderer) {
  bool ready = false;
  for (size_t i = 0; i < count_; ++i)
    ready |= slots_[i].state == State::Ready && slots_[i].loaded == slots_[i].requested;
  if (!ready) return false;
  output_ = Output::Monochrome;
  renderer.setRenderMode(GfxRenderer::BW);
  if (!renderer.isInverted()) {
    if (renderer.getGrayscaleLevels() == 16) {
      if (renderer.beginGrayscale16()) output_ = Output::Native;
    } else if (renderer.getGrayscaleLevels() >= 4 && renderer.grayscaleCapabilities().stripUploads &&
               renderer.getDisplayWidthBytes() <= 256) {
      output_ = Output::Overlay;
    }
  }
  return true;
}

void LuaWeReadCovers::displayFrame(GfxRenderer& renderer) {
  switch (output_) {
    case Output::Monochrome:
      paint(renderer, 2);
      renderer.displayBuffer();
      return;
    case Output::Native:
      paint(renderer, 16);
      if (!renderer.commitGrayscale16()) renderer.displayBuffer();
      return;
    case Output::Overlay:
      paint(renderer, 4);
      break;
  }
  renderer.displayGrayscaleBase();
  uint8_t scratch[256];
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.setRenderMode(mode);
    for (int y = 0; y < renderer.getDisplayHeight(); ++y) {
      renderer.beginStripTarget(scratch, y, 1);
      renderer.clearScreen(0);
      paint(renderer, 4);
      renderer.endStripTarget();
      renderer.writeGrayscalePlaneStrip(mode == GfxRenderer::GRAYSCALE_LSB, scratch, y, 1);
    }
  }
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.cleanupGrayscaleWithFrameBuffer();
}
