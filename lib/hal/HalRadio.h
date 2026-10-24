#pragma once

#include <cstddef>
#include <cstdint>

// Single foreground owner. SPI, expander switching and radio errors stay here.
// Calls are serialized by the chat worker; no SPI/I2C or heap work in an ISR.
class HalRadio {
 public:
  enum class Result : uint8_t { Idle, Received, Sent, Failed };
  static bool supported();
  static uint64_t deviceId();
  // Session IDs/backoff only; not a cryptographic key generator.
  static uint32_t randomNumber();
  bool begin();
  void end();
  bool send(const uint8_t* data, size_t length);
  Result poll(uint8_t* data, size_t capacity, size_t& length, int16_t& rssi);
  bool busy() const { return transmitting_; }

 private:
  bool started_ = false;
  bool transmitting_ = false;
  uint32_t sentAt_ = 0;
};
