#include <CrossStoreRuntime.h>

#include <cassert>
#include <cstring>
#include <fstream>
#include <initializer_list>

#include "crossstore/CatalogPage.h"
#include "crossstore/Package.h"
#include "crossstore/Permissions.h"

namespace crossstore {
// Permission persistence/transport revocation is outside metadata parsing.
uint32_t permissionBit(const char* name) {
  for (const auto& permission : PERMISSIONS)
    if (!std::strcmp(permission.name, name)) return permission.bit;
  return 0;
}
}  // namespace crossstore

int main(int argc, char** argv) {
  using namespace crossstore;
  assert(installedFilename("game2048.csp") && installedFilename("wifi_chat.csp"));
  for (const char* name :
       {"._game2048.csp", ".game2048.csp", "game2048.csp.part", "download.part", "../game.csp", "UPPER.csp"})
    assert(!installedFilename(name));
  JsonDocument manifest;
  assert(deserializeJson(manifest, R"({"schema":1,"api":4,"id":"game2048","version":"0.3.0",
    "name":{"en":"2048","zh":"数字游戏"},"description":{"en":"Board","zh":"棋盘"},
    "runtime":"crossmux-lua54","entry":"main.lua","memory_bytes":65536,"status":"ready",
    "requires":[],"permissions":[]})") == DeserializationError::Ok);
  Package result;
  assert(parsePackage(manifest, result) && result.api == 4);
  manifest["api"] = API_VERSION + 1;
  assert(!parsePackage(manifest, result));
  assert(parsePackage(manifest, result, ManifestUse::Catalog) && result.api == API_VERSION + 1);
  // Looking at a newer app must not weaken installation's default parser.
  assert(!parsePackage(manifest, result, ManifestUse::Install));
  for (int badApi : {0, -1}) {
    manifest["api"] = badApi;
    assert(!parsePackage(manifest, result, ManifestUse::Catalog));
  }
  manifest["api"] = 4;
  I18N.language = Language::ZH_CN;
  assert(parsePackage(manifest, result) && !std::strcmp(result.name, "数字游戏"));
  manifest["id"] = "../outside";
  assert(!parsePackage(manifest, result, ManifestUse::Catalog));
  manifest["id"] = "game2048";
  manifest["requires"].to<JsonArray>().add("unknown_hardware");
  assert(!parsePackage(manifest, result, ManifestUse::Catalog));
  manifest["requires"].to<JsonArray>();
  manifest["permissions"].to<JsonArray>().add("sound");
  manifest["permissions"].add("sound");
  assert(!parsePackage(manifest, result));
  manifest["permissions"].to<JsonArray>();
  manifest["memory_bytes"] = 131072;
  assert(!parsePackage(manifest, result));
  manifest["requires"].add("psram");
  assert(parsePackage(manifest, result));
  manifest["runtime"] = "other-runtime";
  assert(!parsePackage(manifest, result, ManifestUse::Catalog));

  JsonDocument doc;
  assert(deserializeJson(doc, R"({"schema":1,"api":1,"next":"page-2.json","previous":"",
    "page":1,"pages":2,"total":21,"packages":[]})") == DeserializationError::Ok);
  for (unsigned i = 0; i < 16; ++i) doc["packages"].add<JsonObject>();
  CatalogPage page;
  assert(page.read(doc, API_VERSION) && page.total == 21 && page.pages == 2 && page.page == 1);
  for (const char* bad : {"../outside", "/index.json", "https://other/index.json", "page%2ejson", "bad name"}) {
    doc["next"] = bad;
    assert(!page.read(doc, API_VERSION));
  }
  doc["next"] = "page-2.json";
  doc["packages"].add<JsonObject>();
  assert(!page.read(doc, API_VERSION));
  doc["packages"].to<JsonArray>();
  for (unsigned i = 0; i < 5; ++i) doc["packages"].add<JsonObject>();
  doc["page"] = 2;
  doc["previous"] = "index.json";
  doc["next"] = "";
  assert(page.read(doc, API_VERSION) && page.page == 2 && !std::strcmp(page.previous, "index.json"));
  doc["page"] = 3;
  assert(!page.read(doc, API_VERSION));
  doc["page"] = 2;
  doc["total"] = 3;
  assert(!page.read(doc, API_VERSION));
  doc.remove("page");
  doc.remove("pages");
  doc.remove("total");
  doc.remove("previous");
  assert(page.read(doc, API_VERSION) && page.pages == 0);  // legacy third-party source
  doc["page"] = 1;
  assert(!page.read(doc, API_VERSION));  // incomplete counters cannot mislabel a catalog
  char url[256];
  assert(catalogRelativeUrl("https://example.test/store/index.json", "page-2.json", url, sizeof(url)));
  assert(!std::strcmp(url, "https://example.test/store/page-2.json"));
  assert(catalogRelativeUrl("https://example.test/store/page-2.json", "index.json", url, sizeof(url)));
  assert(!std::strcmp(url, "https://example.test/store/index.json"));
  assert(!catalogRelativeUrl("https://example.test/store/index.json", "../secret", url, sizeof(url)));
  assert(!catalogRelativeUrl("https://example.test/store/index.json", "page-2.json", url, 8));
  for (int i = 1; i < argc; ++i) {
    std::ifstream file(argv[i]);
    assert(file && deserializeJson(manifest, file) == DeserializationError::Ok);
    if (manifest["status"] != "ready") continue;
    assert(parsePackage(manifest, result));
    assert(parsePackage(manifest, result, ManifestUse::Catalog));
  }
}
