#include "Package.h"

#include <BoardConfig.h>
#include <CrossStoreRuntime.h>
#include <HalBuzzer.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalMemory.h>
#include <HalRadio.h>
#include <HalRadioAudio.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <ZipFile.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

#include "AppCapabilities.h"
#include "Permissions.h"
#include "RecoverableFile.h"

namespace crossstore {
uint32_t deviceCapabilities() {
  uint32_t result = 0;
  if (HalSystem::supportsWifi()) result |= capabilityBit("wifi");
  if (HalRadio::supported()) result |= capabilityBit("lora");
  const auto chat = appCapabilities::currentDevice();
  if (chat.espNow) result |= capabilityBit("esp_now");
  if (chat.wifiChat) result |= capabilityBit("tox");
  if (halTiltSensor.isAvailable()) result |= capabilityBit("imu");
#if FREEINK_CAP_TOUCH
  result |= capabilityBit("touch");
#endif
  if (Frontlight.present()) result |= capabilityBit("frontlight");
  if (halClock.hasRtc()) result |= capabilityBit("rtc");
  if (HalMemory::getPsramHeap().totalBytes) result |= capabilityBit("psram");
  if (Storage.ready()) result |= capabilityBit("storage");
  if (HalBuzzer::present()) result |= capabilityBit("buzzer");
#if CROSSMUX_RADIO
  if (HalRadioAudio::supports() && HalMemory::getPsramHeap().totalBytes) result |= capabilityBit("radio_audio");
#endif
#if FREEINK_CAP_AUDIO
  result |= capabilityBit("audio");
#else
  if (HalBuzzer::supportsPcm()) result |= capabilityBit("audio");
#endif
  return result;
}

memory::ByteBuffer readEntry(const std::string& path, const char* entry, size_t maximum, size_t& size) {
  ZipFile zip(path);
  size = 0;
  if (!zip.getInflatedFileSize(entry, &size) || !size || size > maximum) return {};
  // At most 4 KiB for metadata / 32 KiB for source. ZipFile's fallible buffer
  // avoids putting scripts on the task stack and is released after compilation.
  memory::ByteBuffer buffer(zip.readFileToMemory(entry, &size, true));
  if (!buffer) LOG_ERR("STORE", "Cannot read package entry: %s", entry);
  return buffer;
}

bool readPackage(const std::string& path, Package& out) {
  size_t size = 0;
  auto raw = readEntry(path, "manifest.json", MAX_MANIFEST_BYTES, size);
  if (!raw) return false;
  JsonDocument doc;
  return deserializeJson(doc, raw.get(), size, DeserializationOption::NestingLimit(5)) == DeserializationError::Ok &&
         parsePackage(doc.as<JsonVariantConst>(), out);
}

bool compatible(const Package& package, char* missing, size_t capacity) {
  if (capacity) missing[0] = 0;
  if (package.api > API_VERSION) {
    if (capacity) std::snprintf(missing, capacity, "%s", tr(STR_STORE_FIRMWARE_UPDATE_REQUIRED));
    return false;
  }
  const uint32_t absent = package.requiredCapabilities & ~deviceCapabilities();
  for (const auto& capability : CAPABILITIES) {
    if (!(absent & capability.bit) || !capacity) continue;
    const size_t used = std::strlen(missing);
    std::snprintf(missing + used, capacity - used, "%s%s", used ? ", " : "", capability.name);
  }
  return !absent;
}

std::string installedPath(const char* id) { return std::string(INSTALL_DIR) + "/" + id + ".csp"; }

bool hasInstallRoom(const char* id) {
  if (Storage.exists(installedPath(id).c_str())) return true;
  auto directory = Storage.open(INSTALL_DIR);
  if (!directory || !directory.isDirectory()) return true;
  size_t count = 0;
  while (auto file = directory.openNextFile()) {
    char name[64]{};
    file.getName(name, sizeof(name));
    if (file.isDirectory() || !installedFilename(name)) continue;
    if (++count >= MAX_INSTALLED_PACKAGES) return false;
  }
  return true;
}

void recoverInstalls() {
  auto dir = Storage.open(INSTALL_DIR);
  // A fresh card has no installation directory yet. HalFile iteration requires
  // an open directory handle; missing storage is an empty recovery scan.
  if (!dir || !dir.isDirectory()) return;
  while (auto file = dir.openNextFile()) {
    char name[64]{};
    file.getName(name, sizeof(name));
    const size_t size = std::strlen(name);
    constexpr char suffix[] = ".csp.previous";
    if (size <= sizeof(suffix) - 1 || std::strcmp(name + size - sizeof(suffix) + 1, suffix)) continue;
    name[size - sizeof(suffix) + 1] = 0;
    if (validId(name)) recoverFile(Storage, installedPath(name));
  }
}

bool verifyHash(const char* path, size_t expectedBytes, const char* expectedHash) {
  if (!expectedBytes || expectedBytes > MAX_PACKAGE_BYTES || std::strlen(expectedHash) != 64) return false;
  auto file = Storage.open(path);
  if (!file || file.size() != expectedBytes) return false;
  uint8_t buffer[512], digest[32];
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  ScopedCleanup cleanup([&] { mbedtls_sha256_free(&context); });
  if (mbedtls_sha256_starts(&context, 0)) return false;
  size_t remaining = expectedBytes;
  while (remaining) {
    const size_t want = std::min(remaining, sizeof(buffer));
    if (file.read(buffer, want) != static_cast<int>(want) || mbedtls_sha256_update(&context, buffer, want))
      return false;
    remaining -= want;
  }
  if (mbedtls_sha256_finish(&context, digest)) return false;
  char hex[65];
  for (unsigned i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return !std::strcmp(hex, expectedHash);
}
}  // namespace crossstore
