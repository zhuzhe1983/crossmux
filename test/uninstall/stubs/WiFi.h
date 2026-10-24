#pragma once
#include <cstdint>
constexpr int WIFI_OFF = 0, WIFI_STA = 1, WL_CONNECTED = 3;
struct FakeWifi {
  int mode_ = WIFI_OFF;
  int getMode() const { return mode_; }
  void mode(int mode) { mode_ = mode; }
  int status() const { return WL_CONNECTED; }
  void begin(const char*, const char*) {}
};
inline FakeWifi WiFi;
inline uint32_t millis() { return 1000; }
