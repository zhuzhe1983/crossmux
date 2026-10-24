#pragma once
#include <cstddef>
#include <cstdint>

// ESP-IDF callbacks only copy into this bounded queue. The chat worker owns
// lifecycle, consumes packets and performs all storage/render-facing work.
class HalEspNow {
 public:
  static constexpr size_t MAX_FRAME = 250;
  struct Frame {
    uint8_t data[MAX_FRAME]{};
    uint16_t length = 0;
    uint64_t sender = 0;
    int8_t rssi = 0;
  };
  enum class TxResult : uint8_t { Idle, Pending, Sent, Failed };
  static bool startWifi();
  static uint8_t channel();
  static uint64_t deviceId();
  static uint32_t randomNumber();
  bool begin();
  void end();
  bool receive(Frame& frame);
  bool send(const uint8_t* data, size_t length);
  TxResult takeTxResult();
  bool busy() const;
  // Called only by the HAL's serialized SDK callback bridge.
  void received(uint64_t sender, int8_t rssi, const uint8_t* data, size_t length);
  void sent(bool success);

 private:
  Frame frames_[8]{};
  uint8_t head_ = 0, count_ = 0;
  bool active_ = false;
  TxResult result_ = TxResult::Idle;
};
