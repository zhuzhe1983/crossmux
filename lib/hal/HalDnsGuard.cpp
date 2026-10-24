#if !defined(SIMULATOR)

#include <esp_netif.h>

#include "Logging.h"

extern "C" void __real_dns_clear_cache();

namespace {
esp_err_t clearDnsCache(void*) {
  __real_dns_clear_cache();
  return ESP_OK;
}
}  // namespace

extern "C" void __wrap_dns_clear_cache() {
  // Arduino-ESP32 3.3.11 NetworkManager::hostByName clears the lwIP DNS
  // cache from its caller's task. Cache eviction can invoke SNTP callbacks
  // synchronously, and those callbacks must hold the TCP/IP core lock.
  // ESP-IDF 5.5.5 checks whether this task already owns that lock before
  // dispatching, so calls from the TCP/IP task do not recursively lock it.
  // This uses IDF's synchronous, stack-backed call; no new task or heap state.
  // Keep the linker --wrap flag in platformio.ini with this compatibility fix.
  const esp_err_t result = esp_netif_tcpip_exec(clearDnsCache, nullptr);
  if (result != ESP_OK) {
    // Never retry the raw operation without its required lock.
    LOG_ERR("DNS", "Cache clear dispatch failed: %d", static_cast<int>(result));
  }
}

#endif
