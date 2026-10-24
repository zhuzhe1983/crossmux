#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

class HalEspNow {
 public:
  struct Frame {
    uint8_t data[250]{};
    uint16_t length = 0;
    uint64_t sender = 0;
    int8_t rssi = 0;
  };
  enum class TxResult { Idle, Pending, Sent, Failed };
  inline static std::deque<Frame> incoming;
  inline static std::vector<std::vector<uint8_t>> outgoing;
  inline static uint8_t testChannel = 6;
  inline static TxResult nextResult = TxResult::Sent;
  inline static uint32_t begins = 0, ends = 0;
  static uint8_t channel() { return testChannel; }
  static uint64_t deviceId() { return 0x112233445566ULL; }
  static uint32_t randomNumber() { return 123; }
  static void reset() {
    incoming.clear();
    outgoing.clear();
    testChannel = 6;
    nextResult = TxResult::Sent;
    begins = ends = 0;
  }
  bool begin() {
    ++begins;
    return true;
  }
  void end() {
    ++ends;
    result_ = TxResult::Idle;
    incoming.clear();
  }
  bool receive(Frame& frame) {
    if (incoming.empty()) return false;
    frame = incoming.front();
    incoming.pop_front();
    return true;
  }
  bool send(const uint8_t* data, size_t length) {
    outgoing.emplace_back(data, data + length);
    result_ = nextResult;
    return true;
  }
  TxResult takeTxResult() {
    const auto result = result_;
    if (result != TxResult::Pending) result_ = TxResult::Idle;
    return result;
  }

 private:
  TxResult result_ = TxResult::Idle;
};
