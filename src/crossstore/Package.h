#pragma once

#include <ArduinoJson.h>
#include <Memory.h>

#include <cstdint>
#include <string>

namespace crossstore {
constexpr char INSTALL_DIR[] = "/.crosspoint/apps";
constexpr char DEFAULT_INDEX[] = "https://crossstore.github.io/CrossStore/index.json";
constexpr size_t MAX_INSTALLED_PACKAGES = 64;
constexpr size_t MAX_PACKAGE_BYTES = 1024 * 1024;
constexpr size_t MAX_MANIFEST_BYTES = 4096;
constexpr size_t MAX_CATALOG_BYTES = 24576;

struct Capability {
  const char* name;
  uint32_t bit;
};
constexpr Capability CAPABILITIES[] = {
    {"buzzer", 1u << 10}, {"audio", 1u << 11},  {"wifi", 1u << 0},        {"lora", 1u << 1},       {"esp_now", 1u << 2},
    {"tox", 1u << 3},     {"imu", 1u << 4},     {"touch", 1u << 5},       {"frontlight", 1u << 6}, {"rtc", 1u << 7},
    {"psram", 1u << 8},   {"storage", 1u << 9}, {"radio_audio", 1u << 12}};

struct Package {
  char id[32]{}, version[24]{}, name[96]{}, description[192]{};
  uint32_t requiredCapabilities = 0;
  uint32_t permissions = 0;
  uint32_t memoryBytes = 0;
  uint32_t api = 1;
};

enum class ManifestUse { Install, Catalog };

uint32_t capabilityBit(const char* name);
uint32_t deviceCapabilities();
bool validId(const char* id);
bool installedFilename(const char* filename);
bool parsePackage(JsonVariantConst value, Package& out, ManifestUse use = ManifestUse::Install);
bool readPackage(const std::string& path, Package& out);
memory::ByteBuffer readEntry(const std::string& path, const char* entry, size_t maximum, size_t& size);
bool compatible(const Package& package, char* missing, size_t capacity);
std::string installedPath(const char* id);
bool verifyHash(const char* path, size_t expectedBytes, const char* expectedHash);
void recoverInstalls();
bool hasInstallRoom(const char* id);
}  // namespace crossstore
