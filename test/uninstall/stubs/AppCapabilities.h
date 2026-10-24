#pragma once
namespace appCapabilities {
enum class Requirement { Radio, WifiChat, EspNow };
inline bool allows(Requirement) { return true; }
}  // namespace appCapabilities
