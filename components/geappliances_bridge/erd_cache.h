// Fixed-size ERD cache backed by a static arena (bump allocator); no
// allocation/deallocation after registration. ERD size is invariant after
// registration — updates are in-place memcpy.
//
// Change detection is done at insert/update time, eliminating per-read
// memcmp overhead.
//
// Thread safety: a single mutex guards entries[] + arena[] + arena_offset.
// Every accessor takes the lock for a short read/copy or write, never across
// the slow MQTT publish.

#ifndef erd_cache_h
#define erd_cache_h

#include <stdint.h>
#include <stdbool.h>

#include "tiny_gea3_erd_client.h"

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif
#ifdef USE_ESP_IDF_STUBS
  #include "esp-idf/freertos_stub.h"
#else
  #include "freertos/FreeRTOS.h"
  #include "freertos/semphr.h"
#endif

#define ERD_CACHE_CAPACITY 300
#define ERD_CACHE_MAX_DATA_SIZE 248
#define ERD_CACHE_ARENA_SIZE 4096

typedef struct {
  tiny_erd_t erd;
  uint8_t board_address;
  uint16_t data_offset;
  uint8_t data_size;
  bool update_required;
  uint8_t publish_cooldown;
  bool valid;
} erd_cache_entry_t;

typedef struct erd_cache_t {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];
  uint8_t arena[ERD_CACHE_ARENA_SIZE];
  uint16_t arena_offset;
  uint32_t update_count;
  uint32_t update_count_window;
  uint32_t required_update_count;
  uint32_t required_update_count_window;
  uint16_t dirty_count;
  uint8_t max_cooldown;
  bool initialized;
  SemaphoreHandle_t lock;
} erd_cache_t;

#ifdef __cplusplus
extern "C" {
#endif

void erd_cache_init(erd_cache_t* self);
void erd_cache_destroy(erd_cache_t* self);

bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, uint8_t board_address, const uint8_t* data, uint8_t data_size);

void erd_cache_clear(erd_cache_t* self);

void erd_cache_set_throttle_rate_seconds(erd_cache_t* self, uint8_t rate);

void erd_cache_mark_published(erd_cache_t* self, erd_cache_entry_t* entry);

void erd_cache_mark_unpublished(erd_cache_t* self, erd_cache_entry_t* entry);

void erd_cache_tick_cooldowns(erd_cache_t* self);

bool erd_cache_snapshot_next_updated(erd_cache_t* self, uint16_t* iterator,
                                     erd_cache_entry_t** entry_out,
                                     tiny_erd_t* erd_out, uint8_t* addr_out,
                                     uint8_t* data_out, uint8_t* size_out);

uint16_t erd_cache_get_count(erd_cache_t* self);
erd_cache_entry_t* erd_cache_get_next_entry(erd_cache_t* self, uint16_t* iterator);

uint32_t erd_cache_get_update_rate(erd_cache_t* self);

uint32_t erd_cache_get_required_update_rate(erd_cache_t* self);
void erd_cache_mark_all_updated(erd_cache_t* self);

uint16_t erd_cache_get_dirty_count(erd_cache_t* self);

const uint8_t* erd_cache_entry_data(const erd_cache_t* self, const erd_cache_entry_t* entry);

uint16_t erd_cache_get_arena_usage(const erd_cache_t* self);

uint8_t erd_cache_get_arena_usage_percent(const erd_cache_t* self);

#ifdef __cplusplus
}
#endif

#endif
