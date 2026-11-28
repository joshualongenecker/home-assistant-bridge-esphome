#ifndef esphome_core_hal_h
#define esphome_core_hal_h

#include "double/esphome_hal_double.hpp"

/* In ESPHome, millis() is available in global namespace.
 * esphome::millis() is provided by esphome_hal_double.hpp. */
inline uint32_t millis() {
  return esphome_hal_double_get_millis();
}

/* Stub delay(0) for tests — yields to main loop in real ESPHome. */
inline void delay(uint32_t ms) {
  (void)ms;
}

namespace esphome {
using ::delay;
}

#endif
