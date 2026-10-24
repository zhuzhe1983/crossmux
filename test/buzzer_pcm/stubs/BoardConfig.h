#pragma once
namespace BoardConfig {
constexpr int PIN_UNASSIGNED = -1;
struct Audio {
  int buzzer = 42;
};
struct Board {
  Audio audio;
};
inline Board ACTIVE;
inline bool hasBuzzer() { return ACTIVE.audio.buzzer != PIN_UNASSIGNED; }
}  // namespace BoardConfig
