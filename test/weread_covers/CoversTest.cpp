#include <GfxRenderer.h>

#include <cassert>
#include <cstdio>

#include "activities/apps/appstore/LuaWeReadCovers.h"

void le16(size_t at, unsigned value) {
  coverFile[at] = value;
  coverFile[at + 1] = value >> 8;
}
void le32(size_t at, uint32_t value) {
  for (int byte = 0; byte < 4; ++byte) coverFile[at + byte] = value >> (byte * 8);
}
void bitmap(unsigned width = 16, unsigned height = 16, bool topDown = true) {
  constexpr unsigned offset = 14 + 40 + 256 * 4;
  const unsigned stride = (width + 3) / 4 * 4;
  coverFile.assign(offset + stride * height, 0);
  coverFile[0] = 'B';
  coverFile[1] = 'M';
  le32(2, coverFile.size());
  le32(10, offset);
  le32(14, 40);
  le32(18, width);
  le32(22, topDown ? -height : height);
  le16(26, 1);
  le16(28, 8);
  le32(34, stride * height);
  le32(46, 256);
  for (unsigned gray = 0; gray < 256; ++gray)
    for (unsigned channel = 0; channel < 3; ++channel) coverFile[54 + gray * 4 + channel] = gray;
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x)
      coverFile[offset + y * stride + x] = ((x + (topDown ? y : height - y - 1)) % 16) * 17;
}
int main() {
  int geometry[4];
  assert(LuaWeReadCovers::parsePlacement("0:1:112:164", geometry));
  for (const char* bad : {"", "-1:1:2:3", "0:0:0:1", "1:2:3", "1:2:3:4x", "1:2:3:4:5", "1:2:3:999999999999999999999",
                          "1: 2:3:4", "1:2:3:32768"})
    assert(!LuaWeReadCovers::parsePlacement(bad, geometry));
  using State = LuaWeReadCovers::State;
  {
    // The native WeRead converter currently writes four-shade 2-bpp BMPs.
    constexpr unsigned offset = 70, stride = 4;
    coverFile.assign(offset + stride * 16, 0);
    coverFile[0] = 'B';
    coverFile[1] = 'M';
    le32(2, coverFile.size());
    le32(10, offset);
    le32(14, 40);
    le32(18, 16);
    le32(22, uint32_t(-16));
    le16(26, 1);
    le16(28, 2);
    le32(46, 4);
    for (unsigned level = 0; level < 4; ++level)
      for (unsigned channel = 0; channel < 3; ++channel) coverFile[54 + level * 4 + channel] = level * 85;
    for (unsigned y = 0; y < 16; ++y)
      for (unsigned x = 0; x < 16; ++x) coverFile[offset + y * stride + x / 4] |= ((x + y) % 4) << (6 - (x % 4) * 2);
    LuaWeReadCovers page;
    assert(page.place(1, 0, 0, 16, 16));
    page.prepare();
    assert(page.load(0, "native-2bpp") == State::Ready);
    GfxRenderer renderer;
    renderer.levels = 16;
    assert(page.beginFrame(renderer));
    page.displayFrame(renderer);
    for (unsigned y = 0; y < 16; ++y)
      for (unsigned x = 0; x < 16; ++x) assert(renderer.native[y * 96 + x] == ((x + y) % 4) * 85);
  }
  for (const bool topDown : {false, true}) {
    for (const int levels : {2, 4, 16}) {
      bitmap(16, 16, topDown);
      LuaWeReadCovers page;
      assert(page.place(1, 8, 12, 16, 16));
      page.prepare();
      assert(page.load(0, "authoritative-cover") == State::Ready);
      GfxRenderer renderer;
      renderer.levels = levels;
      const auto reads = storageOperations, allocations = allocationCalls;
      assert(page.beginFrame(renderer));
      page.displayFrame(renderer);
      assert(reads == storageOperations && allocations == allocationCalls);
      constexpr uint8_t dither[] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
      for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
          const unsigned shade = (x + y) % 16;
          const size_t pixel = (12 + y) * 96 + 8 + x;
          if (levels == 16)
            assert(renderer.native[pixel] == shade * 17);
          else if (levels == 2)
            assert(renderer.pixels[0][pixel] == (shade < 15 && shade <= dither[(y & 3) * 4 + (x & 3)]));
          else {
            const unsigned value = (shade * 3 + 7) / 15;
            assert(renderer.pixels[0][pixel] == (value != 3));
            assert(renderer.written[1][pixel] == (value == 1));
            assert(renderer.written[2][pixel] == (value == 1 || value == 2));
          }
        }
      assert(levels != 4 || renderer.uploads == 192);
      page.clearPlacements();
      assert(!page.beginFrame(renderer));
      page.prepare();
      assert(psramAllocated == 0);
    }
  }
  {
    LuaWeReadCovers page;
    assert(!page.place(0, 0, 0, 16, 16));
    assert(page.place(1, 0, 0, 16, 16));
    page.prepare();
    fileExists = false;
    assert(page.load(0, "missing") == State::Missing);
    fileExists = true;
    bitmap();
    psramAvailable = false;
    assert(page.load(0, "cover") == State::NoMemory && psramAllocated == 0);
    psramAvailable = true;
    for (const auto dimensions : {uint32_t(0), uint32_t(113), uint32_t(0x80000000), uint32_t(-1)}) {
      bitmap();
      le32(18, dimensions);
      assert(page.load(0, "bad-width") == State::Invalid);
    }
    for (const auto dimensions : {uint32_t(0), uint32_t(165), uint32_t(0x80000000), uint32_t(-165)}) {
      bitmap();
      le32(22, dimensions);
      assert(page.load(0, "bad-height") == State::Invalid);
    }
    bitmap();
    coverFile.pop_back();
    assert(page.load(0, "truncated") == State::Invalid);
    bitmap();
    le32(10, 0xfffffff0);
    assert(page.load(0, "bad-offset") == State::Invalid);
    bitmap();
    le32(46, 257);
    assert(page.load(0, "bad-palette") == State::Invalid);
    bitmap();
    assert(page.load(0, "good") == State::Ready);
    GfxRenderer renderer;
    renderer.levels = 16;
    renderer.inverted = true;
    assert(page.beginFrame(renderer));
    page.displayFrame(renderer);
    assert(renderer.displayCalls == 1 && renderer.grayCalls == 0);
    page.clearPlacements();
    assert(page.place(2, 0, 0, 16, 16));
    assert(!page.beginFrame(renderer));  // Never draw the old book in a new slot.
    page.prepare();
    assert(page.state(0) == State::Empty && psramAllocated == 0);
  }
  {
    bitmap(112, 164);
    LuaWeReadCovers page;
    for (size_t i = 0; i < 12; ++i) assert(page.place(i + 1, 0, 0, 112, 164));
    assert(!page.place(13, 0, 0, 112, 164));
    page.prepare();
    for (size_t i = 0; i < 12; ++i) assert(page.load(i, "cover") == State::Ready);
    assert(psramAllocated == 110208 && psramLargest == 9184);
    page.clearPlacements();
    assert(page.place(1, 0, 0, 112, 164));
    page.prepare();
    assert(psramAllocated == 9184);
  }
  assert(psramAllocated == 0);
  puts(
      "WeRead covers: actual BMP parser, 2/4/16-gray pixels, no render I/O/allocation, corruption, OOM and page "
      "release passed");
}
