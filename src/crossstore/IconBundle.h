#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crossstore {
constexpr uint8_t ICON_BUNDLE_HEADER[] = {'C', 'S', 'I', '1', 0, 0, 0, 0};
constexpr size_t ICON_BUNDLE_BYTES = 7048;

struct IconVariant {
  uint8_t levels, size, bits;
  uint16_t offset, bytes;
};
constexpr IconVariant ICON_VARIANTS[] = {
    {2, 32, 1, 8, 128},    {2, 64, 1, 136, 512},   {4, 32, 2, 648, 256},
    {4, 64, 2, 904, 1024}, {16, 32, 4, 1928, 512}, {16, 96, 4, 2440, 4608},
};
static_assert(ICON_VARIANTS[5].offset + ICON_VARIANTS[5].bytes == ICON_BUNDLE_BYTES);

inline bool validIconBundleHeader(const uint8_t* header, size_t headerBytes, size_t bundleBytes) {
  return header && headerBytes == sizeof(ICON_BUNDLE_HEADER) && bundleBytes == ICON_BUNDLE_BYTES &&
         !std::memcmp(header, ICON_BUNDLE_HEADER, sizeof(ICON_BUNDLE_HEADER));
}

inline const IconVariant& iconVariant(int levels, int desiredSize) {
  const unsigned largeIndex = levels >= 16 ? 5 : levels >= 4 ? 3 : 1;
  return ICON_VARIANTS[largeIndex - (desiredSize > 0 && desiredSize <= 32 ? 1 : 0)];
}

inline const IconVariant& largeIconVariant(int levels) { return iconVariant(levels, 0); }
}  // namespace crossstore
