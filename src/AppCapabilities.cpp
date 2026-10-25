#include "AppCapabilities.h"

#include <HalMemory.h>
#include <HalRadio.h>
#include <HalSystem.h>

#include "ChatBuildConfig.h"

appCapabilities::Device appCapabilities::currentDevice() {
  Device device{false, false};
#if CROSSMUX_ESP_CHAT
  device.espNow = HalSystem::supportsWifi();
#endif
#if CROSSMUX_LORA_CHAT
  device.radio = HalRadio::supported();
#endif
#if CROSSMUX_TOX_CHAT && defined(BOARD_HAS_PSRAM)
  device.wifiChat = HalMemory::getPsramHeap().totalBytes >= 4 * 1024 * 1024;
#endif
  return device;
}
