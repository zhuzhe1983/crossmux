#pragma once
#include <atomic>
constexpr int WL_CONNECTED = 3;
struct FakeWiFi {
  std::atomic<int> state{WL_CONNECTED};
  int status() const { return state.load(); }
};
inline FakeWiFi WiFi;
