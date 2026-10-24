#pragma once

#include <array>
#include <cstdint>

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  struct TwoBitPixel {
    bool draw, state;
  };
  static constexpr TwoBitPixel mapTwoBitPixel(RenderMode mode, uint8_t value) {
    // Test display's gray-plane contract; the production table has its own
    // GfxTwoBitMapping tests. Values 0/3 remain in the monochrome base.
    return {mode == GRAYSCALE_MSB ? (value == 1 || value == 2) : value == 1, false};
  }
  std::array<int, 4> clip{0, 0, 96, 96};
  mutable std::array<std::array<uint8_t, 96 * 96>, 3> pixels{};
  mutable std::array<std::array<bool, 96 * 96>, 3> written{};
  mutable std::array<uint8_t, 96 * 96> native{};
  int levels = 4, strip = -1, displayCalls = 0, baseCalls = 0, grayCalls = 0, uploads = 0;
  bool inverted = false, nativeActive = false, nativeAvailable = true, stripAvailable = true;
  RenderMode mode = BW;
  bool glyphIntersectsStrip(int x, int y, int right, int bottom) const {
    return right > clip[0] && x < clip[2] && bottom > clip[1] && y < clip[3] &&
           (strip < 0 || (y <= strip && bottom > strip));
  }
  void drawPixel(int x, int y, bool black) const {
    if (x >= 0 && y >= 0 && x < 96 && y < 96 && glyphIntersectsStrip(x, y, x + 1, y + 1)) {
      pixels[mode][y * 96 + x] = black;
      written[mode][y * 96 + x] = true;
    }
  }
  void drawGrayscale16Pixel(int x, int y, uint8_t gray) const {
    drawPixel(x, y, gray < 128);
    if (x >= 0 && y >= 0 && x < 96 && y < 96 && glyphIntersectsStrip(x, y, x + 1, y + 1)) native[y * 96 + x] = gray;
  }
  void fillRect(int x, int y, int w, int h, bool black) {
    for (int yy = y; yy < y + h; ++yy)
      for (int xx = x; xx < x + w; ++xx) drawPixel(xx, yy, black);
  }
  RenderMode getRenderMode() const { return mode; }
  void setRenderMode(RenderMode value) { mode = value; }
  bool isInverted() const { return inverted; }
  int getGrayscaleLevels() const { return levels; }
  bool beginGrayscale16() {
    nativeActive = nativeAvailable;
    return nativeActive;
  }
  bool commitGrayscale16() {
    ++grayCalls;
    nativeActive = false;
    return true;
  }
  bool isGrayscale16Active() const { return nativeActive; }
  struct Capabilities {
    bool stripUploads;
  };
  Capabilities grayscaleCapabilities() const { return {stripAvailable}; }
  int getDisplayWidthBytes() const { return 12; }
  int getDisplayHeight() const { return 96; }
  const auto& getClipRect() const { return clip; }
  void displayBuffer() { ++displayCalls; }
  void displayGrayscaleBase() { ++baseCalls; }
  void beginStripTarget(uint8_t*, int y, int) { strip = y; }
  void clearScreen(int) {}
  void endStripTarget() { strip = -1; }
  void writeGrayscalePlaneStrip(bool, const uint8_t*, int, int) { ++uploads; }
  void displayGrayBuffer() { ++grayCalls; }
  void cleanupGrayscaleWithFrameBuffer() {}
  class ClipScope {
   public:
    ClipScope(GfxRenderer& target, int x, int y, int right, int bottom) : renderer(target), before(target.clip) {
      target.clip = {x, y, right, bottom};
    }
    ~ClipScope() { renderer.clip = before; }

   private:
    GfxRenderer& renderer;
    std::array<int, 4> before;
  };
};
