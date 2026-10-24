#include <array>
#include <cassert>
#include <cstdio>

#include "crossstore/StoreGrid.h"

namespace {
bool contains(const Rect& outer, const Rect& inner) {
  return inner.width > 0 && inner.height > 0 && inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width && inner.y + inner.height <= outer.y + outer.height;
}

bool disjointWithGap(const Rect& a, const Rect& b, int gap) {
  return a.x + a.width + gap <= b.x || b.x + b.width + gap <= a.x || a.y + a.height + gap <= b.y ||
         b.y + b.height + gap <= a.y;
}

struct Profile {
  const char* name;
  int body, small, control, inset;
};

void checkLayout(const Rect& safe, const Profile& profile) {
  const auto grid = crossstore::StoreGrid::fit(safe, profile.body, profile.small, profile.control, profile.inset);
  assert(grid.capacity > 0 && grid.capacity <= 16);
  assert(grid.rows > 0 && grid.columns > 0);

  const std::array<Rect, 8> bands{grid.all,  grid.installed, grid.settings, grid.status,
                                  grid.body, grid.previous,  grid.page,     grid.next};
  for (const auto& region : bands) assert(contains(safe, region));
  // Includes tabs/settings, status, cards and pager. A positive visible gap
  // means one tap cannot belong to two independently interactive regions.
  for (size_t i = 0; i < bands.size(); ++i)
    for (size_t j = i + 1; j < bands.size(); ++j) assert(disjointWithGap(bands[i], bands[j], 8));

  assert(grid.all.y == grid.installed.y && grid.installed.y == grid.settings.y);
  assert(grid.all.x < grid.installed.x && grid.installed.x < grid.settings.x);
  assert(grid.previous.x < grid.page.x && grid.page.x < grid.next.x);
  assert(grid.previous.height >= 44 && grid.next.height >= 44);
  assert(grid.all.width >= 44 && grid.installed.width >= 44 && grid.settings.width >= 44);
  assert(grid.all.height >= 44 && grid.installed.height >= 44 && grid.settings.height >= 44);

  for (int i = 0; i < grid.capacity; ++i) {
    const auto cell = grid.cell(static_cast<size_t>(i));
    assert(contains(grid.body, cell));
    assert(cell.width >= 44 && cell.height >= 44);
    // Every item can hold a 32px fallback icon and at least one text line.
    assert(cell.height >= 32 + profile.body + 8);
    for (int j = 0; j < i; ++j) assert(disjointWithGap(cell, grid.cell(static_cast<size_t>(j)), 8));
  }
  std::printf("%s %dx%d: %d columns, %d rows, %d icon slots\n", profile.name, safe.width, safe.height, grid.columns,
              grid.rows, grid.capacity);
}

void checkVersions() {
  using crossstore::compareVersions;
  using crossstore::installMark;
  using crossstore::InstallMark;
  const std::array<const char*, 9> versions{"0.0.0", "0.0.9",  "0.0.10",           "0.9.0", "0.10.0", "1.0.0",
                                            "1.9.0", "1.10.0", "99999.99999.99999"};
  for (size_t a = 0; a < versions.size(); ++a) {
    assert(installMark(nullptr, versions[a]) == InstallMark::None);
    assert(installMark("", versions[a]) == InstallMark::None);
    assert(installMark(versions[a], nullptr) == InstallMark::Installed);
    assert(installMark(versions[a], "") == InstallMark::Installed);
    for (size_t b = 0; b < versions.size(); ++b) {
      const int compared = compareVersions(versions[a], versions[b]);
      assert((compared == 0) == (a == b));
      assert((compared < 0) == (a < b));
      assert((compared > 0) == (a > b));
      assert(installMark(versions[a], versions[b]) == (b > a ? InstallMark::Update : InstallMark::Installed));
    }
  }
  // Catalog manifests may be more permissive than the canonical publisher.
  assert(compareVersions("00001.00002.00003", "1.2.3") == 0);
  assert(installMark("1.10.0", "1.9.99") == InstallMark::Installed);
  assert(installMark("1.99.99999", "2.0.0") == InstallMark::Update);
}
}  // namespace

int main() {
  // Production font advances: Ubuntu10=24, Ubuntu12=29, Noto8=23,
  // Noto12=34, Noto14=40. UIScale.h selects these UI families; themes use
  // 40/48/50px tab bands and the high-DPI profile uses a 72px band.
  const std::array<Profile, 5> profiles{{{"INX", 24, 23, 40, 20},
                                         {"Lyra", 29, 24, 48, 20},
                                         {"Rounded", 29, 24, 50, 20},
                                         {"HighDPI", 40, 34, 72, 32},
                                         {"HighDPI-touch", 40, 34, 96, 32}}};
  const std::array<Rect, 6> viewports{Rect{0, 0, 336, 430}, Rect{0, 0, 432, 640}, Rect{0, 0, 912, 420},
                                      Rect{0, 0, 492, 850}, Rect{0, 0, 640, 432}, Rect{0, 0, 420, 912}};
  for (const auto& profile : profiles) {
    for (const auto& viewport : viewports) {
      checkLayout(viewport, profile);
      // Moving the safe area must move every band and card with it; bezel
      // coordinates are not assumed to begin at the physical screen origin.
      const auto shifted = crossstore::StoreGrid::fit(Rect{13, 27, viewport.width, viewport.height}, profile.body,
                                                      profile.small, profile.control, profile.inset);
      const auto origin =
          crossstore::StoreGrid::fit(viewport, profile.body, profile.small, profile.control, profile.inset);
      assert(shifted.capacity == origin.capacity);
      assert(shifted.all.x == origin.all.x + 13 && shifted.all.y == origin.all.y + 27);
      for (int i = 0; i < origin.capacity; ++i) {
        const auto a = origin.cell(static_cast<size_t>(i)), b = shifted.cell(static_cast<size_t>(i));
        assert(b.x == a.x + 13 && b.y == a.y + 27 && a.width == b.width && a.height == b.height);
      }
      checkLayout(Rect{13, 27, viewport.width, viewport.height}, profile);
      // The Activity reserves its outer top inset and system button-hint band
      // before fitting the store. Exercise that smaller rectangle as well.
      checkLayout(Rect{13, 39, viewport.width, viewport.height - 52}, profile);
    }
  }
  checkVersions();
  std::puts("Store grid geometry and installed/update badges passed");
}
