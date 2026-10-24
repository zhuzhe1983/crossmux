#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Memory.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "crossstore/IconBundle.h"
#include "crossstore/Package.h"
#include "crossstore/PackageIcons.h"

namespace crossstore {
memory::ByteBuffer readEntry(const std::string&, const char* entry, size_t maximum, size_t& bytes) {
  for (const auto& variant : ICON_VARIANTS) {
    char name[40];
    std::snprintf(name, sizeof(name), "assets/icon-g%u-%u.bin", variant.levels, variant.size);
    if (std::strcmp(entry, name)) continue;
    assert(maximum == variant.bytes);
    bytes = variant.bytes;
    auto result = memory::makeInternalByteBufferNoThrow(bytes);
    if (result) std::memcpy(result.get(), fileBytes.data() + variant.offset, bytes);
    return result;
  }
  assert(false);
  return {};
}
}  // namespace crossstore

static std::array<uint8_t, crossstore::ICON_BUNDLE_BYTES> bundleBytes() {
  std::array<uint8_t, crossstore::ICON_BUNDLE_BYTES> bytes{};
  std::memcpy(bytes.data(), crossstore::ICON_BUNDLE_HEADER, sizeof(crossstore::ICON_BUNDLE_HEADER));
  for (const auto& variant : crossstore::ICON_VARIANTS) {
    for (unsigned y = 0; y < variant.size; ++y) {
      for (unsigned x = 0; x < variant.size; ++x) {
        const unsigned bit = (y * variant.size + x) * variant.bits;
        const unsigned shade = (x + (variant.size == 32 ? 1 : 0)) % variant.levels;
        bytes[variant.offset + bit / 8] |= shade << (8 - variant.bits - bit % 8);
      }
    }
  }
  return bytes;
}

static void makeBundle() {
  const auto bytes = bundleBytes();
  fileBytes.assign(bytes.begin(), bytes.end());
}

int main() {
  using namespace crossstore;
  makeBundle();
  assert(validIconBundleHeader(fileBytes.data(), 8, fileBytes.size()));
  assert(!validIconBundleHeader(nullptr, 8, fileBytes.size()));
  assert(!validIconBundleHeader(fileBytes.data(), 7, fileBytes.size()));
  assert(!validIconBundleHeader(fileBytes.data(), 8, fileBytes.size() - 1));
  assert(!validIconBundleHeader(fileBytes.data(), 8, fileBytes.size() + 1));
  size_t end = sizeof(ICON_BUNDLE_HEADER);
  for (const auto& variant : ICON_VARIANTS) {
    assert(variant.offset == end);
    assert(variant.bytes == variant.size * variant.size * variant.bits / 8);
    end += variant.bytes;
  }
  assert(end == ICON_BUNDLE_BYTES);
  // Firmware callers supply immutable static bytes; only the selected slot is
  // borrowed, including native 16-gray with no icon allocation/PSRAM available.
  static const auto embedded = bundleBytes();
  PackageIcons icons;
  assert(!icons.loadBundle(PackageIcons::CAPACITY, "bundle", 4));
  assert(!icons.loadBundle(0, nullptr, 4));
  for (int levels : {2, 4, 16}) {
    const auto& variant = largeIconVariant(levels);
    const size_t beforeRead = storageReadBytes;
    assert(icons.loadBundle(0, "bundle", levels));
    assert(storageReadBytes - beforeRead == 8 + variant.bytes);
    GfxRenderer renderer;
    renderer.levels = levels;
    const size_t allocations = memory::allocations, operations = storageOperations;
    icons.beginFrame(renderer);
    icons.paint(renderer, 0, 0, 0, variant.size);
    icons.displayFrame(renderer);
    assert(memory::allocations == allocations && storageOperations == operations);
    for (int x = 0; x < variant.size; ++x) {
      assert(renderer.pixels[GfxRenderer::BW][x] == (levels == 16 ? x % 16 < 8 : x % levels != levels - 1));
      if (levels == 16) assert(renderer.native[x] == (x % 16) * 17);
      if (levels == 4) {
        for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
          const bool expectedWrite = mode == GfxRenderer::GRAYSCALE_MSB ? x % 4 == 1 || x % 4 == 2 : x % 4 == 1;
          assert(renderer.written[mode][x] == expectedWrite);
          assert(!renderer.pixels[mode][x]);
        }
      }
    }
    assert(levels == 2 ? renderer.displayCalls == 1 : renderer.grayCalls == 1);
    assert(renderer.uploads == (levels == 4 ? 192 : 0));
    assert(renderer.mode == GfxRenderer::BW);
    assert(icons.loadPackage(0, "installed.csp", levels));
  }

  for (int levels : {2, 4, 16}) {
    const auto& small = iconVariant(levels, 32);
    assert(small.size == 32);
    assert(iconVariant(levels, 1).size == 32);
    assert(iconVariant(levels, 0).size > 32);
    assert(iconVariant(levels, 33).size > 32);
    assert(iconVariant(levels, 64).size > 32);
    assert(iconVariant(levels, 96).size > 32);
    for (bool fromBundle : {true, false}) {
      const size_t beforeRead = storageReadBytes;
      assert(fromBundle ? icons.loadBundle(0, "bundle", levels, 32)
                        : icons.loadPackage(0, "installed.csp", levels, 32));
      if (fromBundle) assert(storageReadBytes - beforeRead == 8 + small.bytes);
      GfxRenderer renderer;
      renderer.levels = levels;
      icons.beginFrame(renderer);
      icons.paint(renderer, 0, 0, 0, 32);
      icons.displayFrame(renderer);
      for (int x = 0; x < 32; ++x) {
        const unsigned shade = (x + 1) % levels;
        assert(renderer.pixels[GfxRenderer::BW][x] == (levels == 16 ? shade < 8 : shade != unsigned(levels - 1)));
        if (levels == 16) assert(renderer.native[x] == shade * 17);
      }
    }
  }

  for (int levels : {2, 4, 16}) {
    for (int desiredSize : {0, 1, 32, 64, 96}) {
      icons.clear();
      memory::fail = true;
      memory::psram = false;
      failRead = failSeek = true;
      const size_t allocations = memory::allocations, releases = memory::releases;
      const size_t operations = storageOperations;
      assert(icons.loadEmbeddedBundle(0, embedded.data(), embedded.size(), levels, desiredSize));
      const auto& variant = iconVariant(levels, desiredSize);
      GfxRenderer renderer;
      renderer.levels = levels;
      icons.beginFrame(renderer);
      icons.paint(renderer, 0, 0, 0, variant.size);
      icons.displayFrame(renderer);
      for (int x = 0; x < variant.size; ++x) {
        const unsigned shade = (x + (variant.size == 32 ? 1 : 0)) % levels;
        assert(renderer.pixels[GfxRenderer::BW][x] == (levels == 16 ? shade < 8 : shade != unsigned(levels - 1)));
        if (levels == 16) assert(renderer.native[x] == shade * 17);
        if (levels == 4)
          for (auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
            const bool gray = mode == GfxRenderer::GRAYSCALE_MSB ? shade == 1 || shade == 2 : shade == 1;
            assert(renderer.written[mode][x] == gray);
          }
      }
      icons.clear();
      assert(memory::allocations == allocations && memory::releases == releases);
      assert(storageOperations == operations);
      memory::fail = failRead = failSeek = false;
      memory::psram = true;
    }
  }

  // Replacing an owned icon releases it; replacing/clearing borrowed bytes
  // never frees Flash. Invalid input also clears an old borrowed slot.
  assert(icons.loadBundle(0, "bundle", 4));
  const size_t ownedReleases = memory::releases, ownedAllocations = memory::allocations;
  assert(icons.loadEmbeddedBundle(0, embedded.data(), embedded.size(), 4));
  assert(memory::releases == ownedReleases + 1 && memory::allocations == ownedAllocations);
  assert(!icons.loadEmbeddedBundle(PackageIcons::CAPACITY, embedded.data(), embedded.size(), 4));
  icons.clear(PackageIcons::CAPACITY);
  icons.clear(0);
  assert(memory::releases == ownedReleases + 1);
  auto assertFallback = [&icons] {
    GfxRenderer renderer;
    renderer.levels = 2;
    icons.beginFrame(renderer);
    icons.paint(renderer, 0, 0, 0, 32);
    icons.displayFrame(renderer);
    assert(!renderer.written[GfxRenderer::BW][0]);
    assert(renderer.pixels[GfxRenderer::BW][4 * 96 + 4]);
  };
  assertFallback();
  for (size_t length : {size_t{0}, size_t{7}, embedded.size() - 1, embedded.size() + 1}) {
    assert(icons.loadEmbeddedBundle(0, embedded.data(), embedded.size(), 4));
    assert(!icons.loadEmbeddedBundle(0, embedded.data(), length, 4));
    assertFallback();
  }
  assert(!icons.loadEmbeddedBundle(0, nullptr, embedded.size(), 4));
  static auto badHeader = embedded;
  for (size_t byte = 0; byte < sizeof(ICON_BUNDLE_HEADER); ++byte) {
    badHeader[byte] ^= 1;
    assert(!icons.loadEmbeddedBundle(0, badHeader.data(), badHeader.size(), 4));
    badHeader[byte] ^= 1;
  }
  assert(icons.loadEmbeddedBundle(0, embedded.data(), embedded.size(), 4));
  failRead = true;
  assert(!icons.loadBundle(0, "bundle", 4));
  failRead = false;
  assertFallback();
  assert(icons.loadEmbeddedBundle(0, embedded.data(), embedded.size(), 4));
  assert(icons.loadPackage(0, "installed.csp", 4));

  // The recorded clip is reused on the deferred gray strips.
  assert(icons.loadBundle(0, "bundle", 4));
  GfxRenderer clipped;
  clipped.clip = {7, 8, 19, 21};
  icons.beginFrame(clipped);
  icons.paint(clipped, 0, 0, 0, 64);
  clipped.clip = {0, 0, 96, 96};
  icons.displayFrame(clipped);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      if (x < 7 || x >= 19 || y < 8 || y >= 21)
        for (auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB})
          assert(!clipped.written[mode][y * 96 + x]);

  // A font scan / viewport rebuild may replace all positions before display.
  GfxRenderer rebuilt;
  icons.beginFrame(rebuilt);
  icons.paint(rebuilt, 0, 0, 0, 64);
  icons.resetPlacements();
  icons.paint(rebuilt, 0, 20, 0, 64);
  icons.displayFrame(rebuilt);
  assert(!rebuilt.written[GfxRenderer::GRAYSCALE_MSB][1]);
  assert(rebuilt.written[GfxRenderer::GRAYSCALE_MSB][21]);

  // Installation marks overlap the logo, including pixels that originally
  // had intermediate gray. The final strip must remove that modulation.
  static const auto embeddedBadge = [] {
    auto bytes = bundleBytes();
    for (const auto& variant : ICON_VARIANTS) {
      const uint8_t value = variant.levels == 2 ? 0x00 : variant.levels == 4 ? 0x55 : 0x88;
      std::memset(bytes.data() + variant.offset, value, variant.bytes);
    }
    return bytes;
  }();
  for (int levels : {2, 4, 16}) {
    for (int size : {32, 64, 96}) {
      makeBundle();
      const auto& variant = iconVariant(levels, size);
      const uint8_t source = levels == 2 ? 0x00 : levels == 4 ? 0x55 : 0x88;
      std::memset(fileBytes.data() + variant.offset, source, variant.bytes);
      for (bool fromFlash : {false, true}) {
        const size_t beforeLoadAllocations = memory::allocations, beforeLoadOperations = storageOperations;
        assert(fromFlash ? icons.loadEmbeddedBundle(0, embeddedBadge.data(), embeddedBadge.size(), levels, size)
                         : icons.loadBundle(0, "bundle", levels, size));
        if (fromFlash) {
          assert(memory::allocations == beforeLoadAllocations);
          assert(storageOperations == beforeLoadOperations);
        }
        for (auto mark : {InstallMark::None, InstallMark::Installed, InstallMark::Update}) {
          GfxRenderer renderer;
          renderer.levels = levels;
          const size_t allocations = memory::allocations, operations = storageOperations;
          icons.beginFrame(renderer);
          icons.paint(renderer, 0, 0, 0, size, mark);
          icons.displayFrame(renderer);
          assert(memory::allocations == allocations && storageOperations == operations);
          const int diameter = size >= 96 ? 26 : size >= 64 ? 22 : 16;
          const int edge = size - diameter, middle = diameter / 2, quarter = diameter / 4;
          const int white = (edge + diameter * 3 / 4) * 96 + edge + quarter;
          const int black = mark == InstallMark::Installed ? (edge + middle) * 96 + edge + quarter
                                                           : (edge + quarter) * 96 + edge + middle;
          if (mark == InstallMark::None) {
            if (levels == 16) assert(renderer.native[white] == 136);
            if (levels == 4) {
              assert(!renderer.pixels[GfxRenderer::GRAYSCALE_LSB][white]);
              assert(!renderer.pixels[GfxRenderer::GRAYSCALE_MSB][white]);
            }
            continue;
          }
          assert(!renderer.pixels[GfxRenderer::BW][white]);
          assert(renderer.pixels[GfxRenderer::BW][black]);
          if (levels == 16) {
            assert(renderer.native[white] == 255);
            assert(renderer.native[black] == 0);
            assert(renderer.native[edge * 96 + edge] == 136);  // outside the circular badge
          }
          const int outer = diameter - 1;
          for (int y = 0; y < diameter; ++y) {
            for (int x = 0; x < diameter; ++x) {
              const int dx = x * 2 - outer, dy = y * 2 - outer;
              if (dx * dx + dy * dy > outer * outer) continue;
              const int pixel = (edge + y) * 96 + edge + x;
              if (levels == 16) assert(renderer.native[pixel] == 0 || renderer.native[pixel] == 255);
              if (levels == 4) {
                assert(renderer.pixels[GfxRenderer::GRAYSCALE_LSB][pixel]);
                assert(renderer.pixels[GfxRenderer::GRAYSCALE_MSB][pixel]);
                assert(renderer.written[GfxRenderer::GRAYSCALE_LSB][pixel]);
                assert(renderer.written[GfxRenderer::GRAYSCALE_MSB][pixel]);
              }
            }
          }
        }
      }
    }
  }
  makeBundle();

  for (size_t byte = 0; byte < sizeof(ICON_BUNDLE_HEADER); ++byte) {
    fileBytes[byte] ^= 1;
    assert(!icons.loadBundle(0, "bundle", 4));
    fileBytes[byte] ^= 1;
  }
  fileBytes.pop_back();
  assert(!icons.loadBundle(0, "bundle", 4));
  makeBundle();
  failSeek = true;
  assert(!icons.loadBundle(0, "bundle", 4));
  failSeek = false;
  failRead = true;
  assert(!icons.loadBundle(0, "bundle", 4));
  failRead = false;
  memory::fail = true;
  assert(!icons.loadBundle(0, "bundle", 4));
  memory::fail = false;
  memory::psram = false;
  assert(!icons.loadBundle(0, "bundle", 16));
  assert(icons.loadBundle(0, "bundle", 4));
  assert(icons.loadPackage(0, "installed.csp", 4));
  memory::psram = true;
  GfxRenderer inverted;
  inverted.inverted = true;
  icons.beginFrame(inverted);
  icons.paint(inverted, 0, 0, 0, 64);
  icons.displayFrame(inverted);
  assert(inverted.displayCalls == 1 && inverted.uploads == 0);
  icons.clear();
  assert(memory::allocations == memory::releases);
  assert(memory::largest <= 4608);
  std::puts("Icon bundles: offsets, short reads, allocation failure, gray passes and render I/O isolation passed");
}
