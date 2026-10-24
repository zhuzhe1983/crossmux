#pragma once

#include "Arduino.h"

class FakeWire {
 public:
  std::array<uint8_t, 256> registers{};

  bool begin(int sda, int scl, uint32_t speed) {
    touch();
    assert(sda == 41 && scl == 42 && speed == 400000);
    return true;
  }
  void beginTransmission(uint8_t address) {
    touch();
    assert(address == 0x34);
    used_ = 0;
  }
  size_t write(uint8_t value) {
    touch();
    assert(used_ < transmit_.size());
    transmit_[used_++] = value;
    return 1;
  }
  uint8_t endTransmission(bool stop = true) {
    touch();
    assert(used_ == (stop ? 2u : 1u));
    selected_ = transmit_[0];
    if (!stop) return 0;
    const uint8_t value = transmit_[1];
    if (selected_ == 0x90) {
      const bool enabled = (value & 0x03) != 0;
      fixture::events.emplace_back(enabled ? "rail_on" : "rail_off");
      if (!enabled && fixture::failRailOff) return 1;
      // A missing ACK may follow a successful write; cleanup cannot assume off.
      registers[selected_] = value;
      if (enabled && fixture::failRailOn) return 1;
    } else {
      registers[selected_] = value;
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t address, uint8_t count, uint8_t stop) {
    touch();
    assert(address == 0x34 && count == 1 && stop == 1);
    received_ = registers[selected_];
    return 1;
  }
  int read() {
    touch();
    const auto result = received_;
    if (selected_ == 0x90 && fixture::pauseLdoRead.exchange(false)) {
      fixture::ldoReadPaused.store(true);
      fixture::waitFor([] { return fixture::releaseLdoRead.load(); });
    }
    return result;
  }

 private:
  std::array<uint8_t, 2> transmit_{};
  size_t used_ = 0;
  uint8_t selected_ = 0, received_ = 0;
  static void touch() {
    assert(fixture::busDepth > 0);  // Fail if any production PMIC path omits its guard.
    ++fixture::wireCalls;
  }
};
inline FakeWire Wire;
