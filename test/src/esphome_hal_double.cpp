/*!
 * @file
 * @brief ESPHome HAL test double implementation.
 */

#include "double/esphome_hal_double.hpp"
#include "double/critical_depth_shim.hpp"

static uint32_t s_mock_millis = 0;

extern "C" {

void esphome_hal_double_set_millis(uint32_t ms)
{
  s_mock_millis = ms;
}

uint32_t esphome_hal_double_get_millis(void)
{
  return s_mock_millis;
}

unsigned xSemaphoreGive_double_count = 0;

/* Depth of nested host critical sections; see double/critical_depth_shim.hpp. */
int esp_test_critical_depth_ = 0;

}
