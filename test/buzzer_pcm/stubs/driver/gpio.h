#pragma once

#include <cassert>

using gpio_num_t = int;
constexpr int GPIO_MODE_OUTPUT = 1;
inline int gpioLevel = -1;
inline unsigned gpioResets = 0;
inline int gpio_reset_pin(gpio_num_t pin) {
  assert(pin == 42);
  ++gpioResets;
  return 0;
}
inline int gpio_set_direction(gpio_num_t pin, int mode) {
  assert(pin == 42 && mode == GPIO_MODE_OUTPUT);
  return 0;
}
inline int gpio_set_level(gpio_num_t pin, unsigned value) {
  assert(pin == 42 && value == 0);
  gpioLevel = value;
  return 0;
}
