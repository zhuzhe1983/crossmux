#pragma once

#include "Arduino.h"

namespace BoardConfig {
struct Sensors {
  int i2cSda = 41, i2cScl = 42;
  uint32_t i2cHz = 400000;
};
struct Profile {
  Sensors sensors;
};
inline Profile ACTIVE;
inline bool hasAudio() { return fixture::audioPresent; }
}  // namespace BoardConfig
