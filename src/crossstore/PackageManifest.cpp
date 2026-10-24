#include <CrossStoreRuntime.h>
#include <I18n.h>

#include <cstring>

#include "Package.h"
#include "Permissions.h"

namespace crossstore {
namespace {
template <size_t N>
bool field(JsonVariantConst value, char (&out)[N]) {
  if (!value.is<const char*>()) return false;
  const auto text = value.as<JsonString>();
  if (!text.size() || text.size() >= N || std::strlen(text.c_str()) != text.size()) return false;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return true;
}
bool validVersion(const char* text) {
  unsigned dots = 0, digits = 0;
  for (; *text; ++text) {
    if (*text == '.') {
      if (!digits || ++dots > 2) return false;
      digits = 0;
    } else if (*text < '0' || *text > '9' || ++digits > 5)
      return false;
  }
  return dots == 2 && digits;
}
}  // namespace

uint32_t capabilityBit(const char* name) {
  if (!name) return 0;
  for (const auto& capability : CAPABILITIES)
    if (!std::strcmp(name, capability.name)) return capability.bit;
  return 0;
}

bool validId(const char* id) {
  if (!id || id[0] < 'a' || id[0] > 'z') return false;
  const size_t length = std::strlen(id);
  if (length > 31) return false;
  for (size_t i = 1; i < length; ++i)
    if ((id[i] < 'a' || id[i] > 'z') && (id[i] < '0' || id[i] > '9') && id[i] != '_') return false;
  return true;
}

bool installedFilename(const char* filename) {
  if (!filename) return false;
  const size_t length = std::strlen(filename);
  if (length < 5 || length > 35 || std::strcmp(filename + length - 4, ".csp")) return false;
  char id[32]{};
  std::memcpy(id, filename, length - 4);
  return validId(id);
}

bool parsePackage(JsonVariantConst value, Package& out, ManifestUse use) {
  if (value["schema"] != 1 || !value["api"].is<unsigned>() || value["api"].as<unsigned>() < 1 ||
      (use == ManifestUse::Install && value["api"].as<unsigned>() > API_VERSION) ||
      value["runtime"] != "crossmux-lua54" || value["entry"] != "main.lua" || value["status"] != "ready")
    return false;
  const char* language = I18N.getLanguage() == Language::ZH_CN ? "zh" : "en";
  Package parsed;
  parsed.api = value["api"].as<unsigned>();
  if (!field(value["id"], parsed.id) || !validId(parsed.id) || !field(value["version"], parsed.version) ||
      !validVersion(parsed.version) || !field(value["name"][language], parsed.name) ||
      !field(value["description"][language], parsed.description) || !value["memory_bytes"].is<uint32_t>())
    return false;
  parsed.memoryBytes = value["memory_bytes"].as<uint32_t>();
  if (parsed.memoryBytes < 16384 || parsed.memoryBytes > MAX_VM_BYTES) return false;
  if (!value["requires"].is<JsonArrayConst>() || value["requires"].size() > std::size(CAPABILITIES)) return false;
  for (JsonVariantConst requirement : value["requires"].as<JsonArrayConst>()) {
    if (!requirement.is<const char*>()) return false;
    const uint32_t bit = capabilityBit(requirement.as<const char*>());
    if (!bit || (parsed.requiredCapabilities & bit)) return false;
    parsed.requiredCapabilities |= bit;
  }
  if (!value["permissions"].isNull()) {
    if (!value["permissions"].is<JsonArrayConst>() || value["permissions"].size() > std::size(PERMISSIONS))
      return false;
    for (JsonVariantConst permission : value["permissions"].as<JsonArrayConst>()) {
      if (!permission.is<const char*>()) return false;
      const auto bit = permissionBit(permission.as<const char*>());
      if (!bit || (parsed.permissions & bit)) return false;
      parsed.permissions |= bit;
    }
  }
  if (parsed.memoryBytes > 65536 && !(parsed.requiredCapabilities & capabilityBit("psram"))) return false;
  if (value["api"].as<unsigned>() < 3 &&
      (parsed.permissions & (permissionBit("files_export") | permissionBit("keep_awake"))))
    return false;
  out = parsed;
  return true;
}

}  // namespace crossstore
