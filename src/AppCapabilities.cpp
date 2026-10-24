#include "AppCapabilities.h"

#include <HalMemory.h>
#include <HalRadio.h>

#include "ChatBuildConfig.h"

appCapabilities::Device appCapabilities::currentDevice() {
  Device device{false, false};
#if CROSSMUX_ESP_CHAT && defined(CONFIG_IDF_TARGET_ESP32S3)
  device.espNow = true;
#endif
#if CROSSMUX_LORA_CHAT
  device.radio = HalRadio::supported();
#endif
#if CROSSMUX_TOX_CHAT && defined(BOARD_HAS_PSRAM)
  device.wifiChat = HalMemory::getPsramHeap().totalBytes >= 4 * 1024 * 1024;
#endif
  return device;
}
