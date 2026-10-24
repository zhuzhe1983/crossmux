#pragma once

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace crossstore {
constexpr size_t CATALOG_PAGE_SIZE = 16;

inline bool catalogString(JsonVariantConst value, char* out, size_t capacity) {
  if (!value.is<const char*>()) return false;
  const JsonString text = value.as<JsonString>();
  if (text.size() >= capacity || std::strlen(text.c_str()) != text.size()) return false;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return true;
}

inline bool validCatalogRelative(const char* path) {
  if (!*path || *path == '/' || std::strstr(path, "..") || std::strpbrk(path, ":\\?#%")) return false;
  for (; *path; ++path)
    if (static_cast<unsigned char>(*path) <= 32) return false;
  return true;
}

inline bool catalogRelativeUrl(const char* base, const char* relative, char* out, size_t capacity) {
  if (!validCatalogRelative(relative)) return false;
  const char* slash = std::strrchr(base, '/');
  if (!slash || slash < base + 8) return false;
  return std::snprintf(out, capacity, "%.*s%s", static_cast<int>(slash - base + 1), base, relative) <
         static_cast<int>(capacity);
}

// Bounded page metadata, held with the store's existing one-page entry cache.
// Old static catalogs with only `next` remain supported; no total is invented.
struct CatalogPage {
  char next[160]{}, previous[160]{};
  unsigned page = 0, pages = 0, total = 0;

  bool read(JsonVariantConst document, unsigned runtimeApi) {
    *this = {};
    if (document["schema"] != 1 || !document["api"].is<unsigned>() || document["api"].as<unsigned>() < 1 ||
        document["api"].as<unsigned>() > runtimeApi || !document["packages"].is<JsonArrayConst>() ||
        document["packages"].size() > CATALOG_PAGE_SIZE || !catalogString(document["next"], next, sizeof(next)) ||
        (*next && !validCatalogRelative(next)))
      return false;
    if (!document["previous"].isNull() && (!catalogString(document["previous"], previous, sizeof(previous)) ||
                                           (*previous && !validCatalogRelative(previous))))
      return false;
    const bool counters = !document["page"].isNull() || !document["pages"].isNull() || !document["total"].isNull();
    if (!counters) return true;
    if (!document["page"].is<unsigned>() || !document["pages"].is<unsigned>() || !document["total"].is<unsigned>())
      return false;
    page = document["page"];
    pages = document["pages"];
    total = document["total"];
    return page >= 1 && page <= pages && pages <= 4096 && total <= pages * CATALOG_PAGE_SIZE &&
           total >= document["packages"].size() && (page == 1) == !*previous && (page == pages) == !*next;
  }
};
}  // namespace crossstore
