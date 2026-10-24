#pragma once

#include <cstdint>

namespace appCapabilities {
enum class Requirement : uint8_t { None = 0, Radio = 1, WifiChat = 2, EspNow = 3 };
struct Device {
  bool radio;
  bool wifiChat;
  bool espNow = false;
};
constexpr bool allows(Requirement requirement, Device device) {
  switch (requirement) {
    case Requirement::None:
      return true;
    case Requirement::Radio:
      return device.radio;
    case Requirement::WifiChat:
      return device.wifiChat;
    case Requirement::EspNow:
      return device.espNow;
  }
  return false;
}
Device currentDevice();
inline bool allows(Requirement requirement) { return allows(requirement, currentDevice()); }
}  // namespace appCapabilities
