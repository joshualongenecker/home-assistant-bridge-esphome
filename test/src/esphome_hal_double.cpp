/*!
 * @file
 * @brief ESPHome HAL test double implementation.
 */

#include "double/esphome_hal_double.hpp"
#include "double/critical_depth_shim.hpp"
#include <cstring>

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

/* Recording behind the ESP_LOGx stubs; see double/critical_depth_shim.hpp.
 * Tags are matched by string because every translation unit has its own TAG. */
typedef struct {
  const char* tag;
  int count;
  int last_depth;
  int per_depth[8];  /* calls at depth 0..7; deeper depths are not binned */
} esp_test_log_slot_;

#define ESP_TEST_LOG_DEPTH_BINS 8

static esp_test_log_slot_ s_log_slots_[24];
static int s_log_slots_used_ = 0;

void esp_test_log_record_(const char* tag, int depth)
{
  for (int i = 0; i < s_log_slots_used_; i++) {
    if (std::strcmp(s_log_slots_[i].tag, tag) == 0) {
      s_log_slots_[i].count++;
      s_log_slots_[i].last_depth = depth;
      if (depth >= 0 && depth < ESP_TEST_LOG_DEPTH_BINS) {
        s_log_slots_[i].per_depth[depth]++;
      }
      return;
    }
  }
  if (s_log_slots_used_ < (int)(sizeof(s_log_slots_) / sizeof(s_log_slots_[0]))) {
    s_log_slots_[s_log_slots_used_].tag = tag;
    s_log_slots_[s_log_slots_used_].count = 1;
    s_log_slots_[s_log_slots_used_].last_depth = depth;
    if (depth >= 0 && depth < ESP_TEST_LOG_DEPTH_BINS) {
      s_log_slots_[s_log_slots_used_].per_depth[depth] = 1;
    }
    s_log_slots_used_++;
  }
  /* Slots are exhausted: the next count lookup for a new tag returns 0, which
   * makes a count assertion fail (spurious red, never a false pass). Raise the
   * cap if a suite legitimately logs more than 24 distinct tags. */
}

void esp_test_log_reset_(void)
{
  s_log_slots_used_ = 0;
}

static const esp_test_log_slot_* esp_test_log_find_(const char* tag)
{
  for (int i = 0; i < s_log_slots_used_; i++) {
    if (std::strcmp(s_log_slots_[i].tag, tag) == 0) {
      return &s_log_slots_[i];
    }
  }
  return nullptr;
}

int esp_test_log_count_at_depth_(const char* tag, int depth)
{
  const esp_test_log_slot_* slot = esp_test_log_find_(tag);
  return (slot && depth >= 0 && depth < ESP_TEST_LOG_DEPTH_BINS) ? slot->per_depth[depth] : 0;
}

int esp_test_log_depth_(const char* tag)
{
  const esp_test_log_slot_* slot = esp_test_log_find_(tag);
  return slot ? slot->last_depth : -1;
}

}
