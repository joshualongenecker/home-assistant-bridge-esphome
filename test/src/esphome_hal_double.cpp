/*!
 * @file
 * @brief ESPHome HAL test double implementation.
 */

#include "double/esphome_hal_double.hpp"
#include "double/critical_depth_shim.hpp"
#include <pthread.h>
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

/* Host critical-section depth plus the log recorder behind the ESP_LOGx stubs.
 * Both are shared static state, and the concurrency suites run real threads that
 * log and enter critical sections, so every access goes through these
 * mutex-guarded accessors -- ThreadSanitizer flagged the raw globals.
 * See double/critical_depth_shim.hpp for why the depth is tracked at all. */
static pthread_mutex_t s_test_state_mutex_ = PTHREAD_MUTEX_INITIALIZER;
static int s_critical_depth_ = 0;

int esp_test_critical_depth_get_(void)
{
  pthread_mutex_lock(&s_test_state_mutex_);
  int depth = s_critical_depth_;
  pthread_mutex_unlock(&s_test_state_mutex_);
  return depth;
}

void esp_test_critical_depth_add_(int delta)
{
  pthread_mutex_lock(&s_test_state_mutex_);
  s_critical_depth_ += delta;
  pthread_mutex_unlock(&s_test_state_mutex_);
}

void esp_test_critical_depth_reset_(void)
{
  pthread_mutex_lock(&s_test_state_mutex_);
  s_critical_depth_ = 0;
  pthread_mutex_unlock(&s_test_state_mutex_);
}

/* Tags are matched by string because every translation unit has its own TAG.
 * All slot access below happens with s_test_state_mutex_ held. */
typedef struct {
  const char* tag;
  int count;
  int last_depth;
  int per_depth[8];  /* calls at depth 0..7; deeper depths are not binned */
} esp_test_log_slot_;

#define ESP_TEST_LOG_DEPTH_BINS 8

static esp_test_log_slot_ s_log_slots_[24];
static int s_log_slots_used_ = 0;

static esp_test_log_slot_* esp_test_log_find_(const char* tag)
{
  for (int i = 0; i < s_log_slots_used_; i++) {
    if (std::strcmp(s_log_slots_[i].tag, tag) == 0) {
      return &s_log_slots_[i];
    }
  }
  return nullptr;
}

void esp_test_log_record_(const char* tag, int depth)
{
  pthread_mutex_lock(&s_test_state_mutex_);
  esp_test_log_slot_* slot = esp_test_log_find_(tag);
  if (slot != nullptr) {
    slot->count++;
    slot->last_depth = depth;
    if (depth >= 0 && depth < ESP_TEST_LOG_DEPTH_BINS) {
      slot->per_depth[depth]++;
    }
  } else if (s_log_slots_used_ < (int)(sizeof(s_log_slots_) / sizeof(s_log_slots_[0]))) {
    /* Registration zeroes the whole slot: esp_test_log_reset_() only rewinds
     * s_log_slots_used_, so a reused slot must not inherit an earlier test's
     * per-depth counts. */
    slot = &s_log_slots_[s_log_slots_used_++];
    slot->tag = tag;
    slot->count = 1;
    slot->last_depth = depth;
    for (int i = 0; i < ESP_TEST_LOG_DEPTH_BINS; i++) {
      slot->per_depth[i] = 0;
    }
    if (depth >= 0 && depth < ESP_TEST_LOG_DEPTH_BINS) {
      slot->per_depth[depth] = 1;
    }
  }
  /* Slots exhausted: a lookup for a new tag then returns 0, which makes a count
   * assertion fail (spurious red, never a false pass). Raise the cap if a suite
   * legitimately logs more than 24 distinct tags without an intervening reset. */
  pthread_mutex_unlock(&s_test_state_mutex_);
}

void esp_test_log_reset_(void)
{
  pthread_mutex_lock(&s_test_state_mutex_);
  s_log_slots_used_ = 0;
  pthread_mutex_unlock(&s_test_state_mutex_);
}

int esp_test_log_count_(const char* tag)
{
  pthread_mutex_lock(&s_test_state_mutex_);
  const esp_test_log_slot_* slot = esp_test_log_find_(tag);
  int count = slot ? slot->count : 0;
  pthread_mutex_unlock(&s_test_state_mutex_);
  return count;
}

int esp_test_log_count_at_depth_(const char* tag, int depth)
{
  pthread_mutex_lock(&s_test_state_mutex_);
  const esp_test_log_slot_* slot = esp_test_log_find_(tag);
  int count = (slot && depth >= 0 && depth < ESP_TEST_LOG_DEPTH_BINS) ? slot->per_depth[depth] : 0;
  pthread_mutex_unlock(&s_test_state_mutex_);
  return count;
}

int esp_test_log_depth_(const char* tag)
{
  pthread_mutex_lock(&s_test_state_mutex_);
  const esp_test_log_slot_* slot = esp_test_log_find_(tag);
  int last = slot ? slot->last_depth : -1;
  pthread_mutex_unlock(&s_test_state_mutex_);
  return last;
}

}
