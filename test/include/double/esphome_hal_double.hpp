/*!
 * @file
 * @brief ESPHome HAL test double.
 *
 * Provides mock implementations of esphome::millis() and ESP_LOG* macros
 * so that code depending on ESPHome HAL can be unit tested.
 *
 * Usage:
 *   #include "double/esphome_hal_double.hpp"
 *
 *   // Set the simulated time.
 *   esphome_hal_double_set_millis(1000);
 *
 *   // Expect a log call.
 *   mock().expectOneCall("ESP_LOGI").withStringParameter("tag", "my_tag");
 */

#ifndef esphome_hal_double_hpp
#define esphome_hal_double_hpp

#include <cstdint>
#include "double/critical_depth_shim.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Set the simulated millis() value.
 */
void esphome_hal_double_set_millis(uint32_t ms);

/*!
 * Get the current simulated millis() value.
 */
uint32_t esphome_hal_double_get_millis(void);

#ifdef __cplusplus
}
#endif

/* Override the esphome::millis() function. */
namespace esphome {
inline uint32_t millis() {
  return esphome_hal_double_get_millis();
}
}

/* ESP_LOG* stubs. These are NOT no-ops: each records the tag and the
 * critical-section depth at the call site, so a test can assert that a warning
 * was emitted at depth 0 (issue #198: the crash was a log taken inside a
 * critical section). Read back with esp_test_log_count_() /
 * esp_test_log_depth_(); reset with esp_test_log_reset_(). Test files that want
 * mock() based log expectations include CppUTest first and redefine these. */
#define ESP_LOG_TAG "test"
#define ESP_LOGE(tag, ...) esp_test_log_record_((tag), esp_test_critical_depth_get_())
#define ESP_LOGW(tag, ...) esp_test_log_record_((tag), esp_test_critical_depth_get_())
#define ESP_LOGI(tag, ...) esp_test_log_record_((tag), esp_test_critical_depth_get_())
#define ESP_LOGD(tag, ...) esp_test_log_record_((tag), esp_test_critical_depth_get_())
#define ESP_LOGV(tag, ...) esp_test_log_record_((tag), esp_test_critical_depth_get_())
#define ESP_LOGCONFIG(tag, ...) esp_test_log_record_((tag), esp_test_critical_depth_get_())

#endif  // esphome_hal_double_hpp
