#include "HalEspNow.h"

#if defined(CROSSMUX_ESP_CHAT) && CROSSMUX_ESP_CHAT
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>

#include <cstring>

namespace {
portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
HalEspNow* owner = nullptr;
constexpr uint8_t broadcast[6] = {255, 255, 255, 255, 255, 255};
uint64_t macValue(const uint8_t* mac) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 6; ++i) value = (value << 8) | mac[i];
  return value;
}
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  if (!info || !info->src_addr || !data || length <= 0 || length > 250) return;
  portENTER_CRITICAL(&guard);
  if (owner) owner->received(macValue(info->src_addr), info->rx_ctrl ? info->rx_ctrl->rssi : 0, data, length);
  portEXIT_CRITICAL(&guard);
}
void onSent(const esp_now_send_info_t*, esp_now_send_status_t status) {
  portENTER_CRITICAL(&guard);
  if (owner) owner->sent(status == ESP_NOW_SEND_SUCCESS);
  portEXIT_CRITICAL(&guard);
}
}  // namespace
bool HalEspNow::startWifi() {
  if (WiFi.getMode() != WIFI_OFF) return (WiFi.getMode() & WIFI_STA) != 0;
  // Only a newly owned offline STA gets the default channel. Never retune an AP connection.
  if (!WiFi.mode(WIFI_STA)) return false;
  if (esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE) == ESP_OK) return true;
  WiFi.mode(WIFI_OFF);
  return false;
}
uint8_t HalEspNow::channel() {
  if (!(WiFi.getMode() & WIFI_STA)) return 0;
  uint8_t primary = 0;
  wifi_second_chan_t secondary;
  return esp_wifi_get_channel(&primary, &secondary) == ESP_OK ? primary : 0;
}
uint64_t HalEspNow::deviceId() {
  uint8_t mac[6]{};
  return esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK ? macValue(mac) : 0;
}
uint32_t HalEspNow::randomNumber() { return esp_random(); }
bool HalEspNow::begin() {
  if (active_ || !channel()) return false;
  portENTER_CRITICAL(&guard);
  const bool available = owner == nullptr;
  if (available) owner = this;
  portEXIT_CRITICAL(&guard);
  if (!available) return false;
  if (esp_now_init() != ESP_OK) {
    portENTER_CRITICAL(&guard);
    owner = nullptr;
    portEXIT_CRITICAL(&guard);
    return false;
  }
  active_ = true;
  esp_now_peer_info_t peer{};
  std::memcpy(peer.peer_addr, broadcast, 6);
  peer.ifidx = WIFI_IF_STA;
  peer.channel = 0;
  if (esp_now_register_recv_cb(onReceive) != ESP_OK || esp_now_register_send_cb(onSent) != ESP_OK ||
      esp_now_add_peer(&peer) != ESP_OK) {
    end();
    return false;
  }
  LOG_INF("ESPNOW", "Ready, channel=%u, RX queue=%u bytes", channel(), static_cast<unsigned>(sizeof(frames_)));
  return true;
}
void HalEspNow::end() {
  if (!active_) return;
  // Clear under the same lock used throughout callbacks before freeing this owner.
  portENTER_CRITICAL(&guard);
  owner = nullptr;
  count_ = 0;
  head_ = 0;
  result_ = TxResult::Idle;
  portEXIT_CRITICAL(&guard);
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_deinit();
  active_ = false;
}
void HalEspNow::received(uint64_t sender, int8_t rssi, const uint8_t* data, size_t length) {
  if (count_ == 8) return;
  auto& frame = frames_[(head_ + count_) % 8];
  std::memcpy(frame.data, data, length);
  frame.length = length;
  frame.sender = sender;
  frame.rssi = rssi;
  ++count_;
}
void HalEspNow::sent(bool success) { result_ = success ? TxResult::Sent : TxResult::Failed; }
bool HalEspNow::receive(Frame& frame) {
  portENTER_CRITICAL(&guard);
  const bool ready = count_ != 0;
  if (ready) {
    frame = frames_[head_];
    head_ = (head_ + 1) % 8;
    --count_;
  }
  portEXIT_CRITICAL(&guard);
  return ready;
}
bool HalEspNow::send(const uint8_t* data, size_t length) {
  if (!active_ || !data || !length || length > MAX_FRAME || busy()) return false;
  portENTER_CRITICAL(&guard);
  result_ = TxResult::Pending;
  portEXIT_CRITICAL(&guard);
  if (esp_now_send(broadcast, data, length) == ESP_OK) return true;
  portENTER_CRITICAL(&guard);
  result_ = TxResult::Idle;
  portEXIT_CRITICAL(&guard);
  return false;
}
bool HalEspNow::busy() const {
  portENTER_CRITICAL(&guard);
  const bool value = result_ == TxResult::Pending;
  portEXIT_CRITICAL(&guard);
  return value;
}
HalEspNow::TxResult HalEspNow::takeTxResult() {
  portENTER_CRITICAL(&guard);
  const auto value = result_;
  if (value != TxResult::Pending) result_ = TxResult::Idle;
  portEXIT_CRITICAL(&guard);
  return value;
}
#else
bool HalEspNow::startWifi() { return false; }
uint8_t HalEspNow::channel() { return 0; }
uint64_t HalEspNow::deviceId() { return 0; }
uint32_t HalEspNow::randomNumber() { return 0; }
bool HalEspNow::begin() { return false; }
void HalEspNow::end() {}
bool HalEspNow::receive(Frame&) { return false; }
bool HalEspNow::send(const uint8_t*, size_t) { return false; }
bool HalEspNow::busy() const { return false; }
HalEspNow::TxResult HalEspNow::takeTxResult() { return TxResult::Idle; }
void HalEspNow::received(uint64_t, int8_t, const uint8_t*, size_t) {}
void HalEspNow::sent(bool) {}
#endif
