/*!
 * @file
 * @brief Fixed-size ERD cache with static arena storage.
 *
 * Stores the latest data for up to ERD_CACHE_CAPACITY ERDs. All ERD data is
 * stored in a contiguous static arena (bump allocator). ERDs > 248 bytes
 * (GEA3 max payload) are rejected. ERD size is invariant after registration —
 * updates are in-place memcpy with no allocation or deallocation. Change
 * detection is done at insert/update time, eliminating per-read memcmp overhead.
 *
 * Thread safety: The cache is accessed from both the main loop and
 * the background MQTT publisher task. On dual-core ESP32 the publisher
 * is pinned to Core 1 (same core as ESPHome's main loop task) via
 * xTaskCreateStaticPinnedToCore, so both access paths run on the same
 * core and cannot execute in parallel. No mutex is needed. */

#ifndef erd_cache_h
#define erd_cache_h

#include <stdint.h>
#include <stdbool.h>

#include "tiny_gea3_erd_client.h"

#define ERD_CACHE_CAPACITY 300
#define ERD_CACHE_MAX_DATA_SIZE 248    /* GEA3 max payload: 255 - 7 byte overhead */
#define ERD_CACHE_ARENA_SIZE 4096      /* Static arena for all ERD data */

typedef struct {
  tiny_erd_t erd;
  uint16_t data_offset;     /* offset into arena */
  uint8_t data_size;        /* invariant after registration */
  bool update_required;
  uint8_t publish_cooldown; /* counts down from max_cooldown to 0; 0 = eligible */
  bool valid;
} erd_cache_entry_t;

typedef struct erd_cache_t {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];
  uint8_t arena[ERD_CACHE_ARENA_SIZE]; /* static arena for all ERD data */
  uint16_t arena_offset;               /* next free byte in arena (bump pointer) */
  uint32_t update_count;               /* total cache updates since init */
  uint32_t update_count_window;        /* updates since last get_update_rate() call */
  uint32_t required_update_count;      /* total updates setting update_required=true since init */
  uint32_t required_update_count_window; /* such updates since last get_required_update_rate() call */
  uint8_t max_cooldown;                /* configured rate limit in seconds; 0 = disabled */
  bool initialized;                    /* true after first successful erd_cache_init() */
} erd_cache_t;

#ifdef __cplusplus
extern "C" {
#endif

void erd_cache_init(erd_cache_t* self);
void erd_cache_destroy(erd_cache_t* self);

/* Updates or inserts ERD data.
 * Always marks update_required only when data has changed.
 * New entries always mark update_required=true.
 * Returns true if update_required was set (or entry was new).
 * Returns false if:
 *   - data_size > ERD_CACHE_MAX_DATA_SIZE (248 bytes, GEA3 limit)
 *   - cache is full (300 entries)
 *   - arena is full (4096 bytes)
 *   - data is unchanged
 *   - ERD size changed (appliance lost) */
bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size);

/* Set the minimum interval (in seconds) between publishes for any ERD.
 * 0 = disabled (publish on every update). Range: 0-255. */
void erd_cache_set_throttle_rate_seconds(erd_cache_t* self, uint8_t rate);

/* Mark an ERD entry as successfully published to MQTT.
 * Reloads the publish_cooldown timer. Call after mqtt_client_publish_raw() succeeds.
 * Static inline — zero overhead when max_cooldown is 0 (early return).
 *
 * Thread safety: with the ESP-IDF framework this is called from the background MQTT publisher
 * task while tick_cooldowns() runs from the main loop.  On dual-core ESP32 the publisher
 * is pinned to Core 1 (same core as ESPHome's main loop) via
 * xTaskCreateStaticPinnedToCore, so both access paths run on the same core.
 * The tick→signal_work ordering in loop() ensures the tick always runs before
 * the task drains. No additional locking is needed. */
static inline void erd_cache_mark_published(erd_cache_t* self, erd_cache_entry_t* entry) {
  if (self->max_cooldown == 0 || entry == NULL) return;
  entry->publish_cooldown = self->max_cooldown;
}

/* Mark an ERD entry as NOT published (publish failed or was dropped).
 * Re-sets update_required so the entry is picked up on the next iteration.
 * Call when mqtt_client_publish_raw() returns false.
 * Static inline — trivial operation, no locking needed (same thread that
 * cleared update_required via erd_cache_get_next_updated() is the only
 * caller). */
static inline void erd_cache_mark_unpublished(erd_cache_t* self, erd_cache_entry_t* entry) {
  (void)self;
  if (entry != NULL) {
    entry->update_required = true;
  }
}

/* Decrement publish_cooldown for all entries with update_required=true.
 * Call once per second. Static inline — zero overhead when max_cooldown is 0.
 *
 * Thread safety: see erd_cache_mark_published() above.  On dual-core ESP32 both the
 * publisher task and the main loop run on Core 1 (task is pinned), so they cannot
 * execute in parallel. tick_cooldowns touches entries with update_required=true;
 * mark_published touches entries whose update_required was just cleared — disjoint sets. */
static inline void erd_cache_tick_cooldowns(erd_cache_t* self) {
  if (self->max_cooldown == 0) return;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->update_required && e->publish_cooldown > 0) {
      e->publish_cooldown--;
    }
  }
}

/* Returns the next entry with update_required=true, then clears the flag.
 * Caller provides an iterator (uint16_t) initialized to 0.
 * Returns NULL when no more updated entries remain.
 * NOTE: Declared for future use (e.g., batch republish after MQTT reconnect).
 *       Not used in the initial implementation. */
erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator);

/* Returns the number of valid entries currently in the cache. */
uint16_t erd_cache_get_count(erd_cache_t* self);
/* Returns the next valid entry in the cache, iterating all entries.
 * Caller provides an iterator (uint16_t) initialized to 0.
 * Returns NULL when no more valid entries remain (resets iterator to 0).
 * Unlike erd_cache_get_next_updated(), this does NOT require update_required=true
 * and does NOT clear any flags — it is a read-only iteration. */
erd_cache_entry_t* erd_cache_get_next_entry(erd_cache_t* self, uint16_t* iterator);

/* Returns the number of cache updates since the last call, then resets the window counter. */
uint32_t erd_cache_get_update_rate(erd_cache_t* self);

/* Returns the number of updates that set update_required=true since the last call, then resets the window counter. */
uint32_t erd_cache_get_required_update_rate(erd_cache_t* self);
/* Mark all valid entries as needing republish. Used after a long MQTT
 * disconnect to force a full drain of retained values to the broker. */
void erd_cache_mark_all_updated(erd_cache_t* self);

/* Returns a pointer to the ERD data in the arena.
 * Static inline — zero overhead, direct pointer arithmetic.
 * Returns NULL if entry is NULL or entry is not valid. */
static inline const uint8_t* erd_cache_entry_data(const erd_cache_t* self, const erd_cache_entry_t* entry) {
  if (entry == NULL || !entry->valid) return NULL;
  return &self->arena[entry->data_offset];
}

/* Returns the number of bytes currently used in the arena.
 * Useful for monitoring arena utilization. */
static inline uint16_t erd_cache_get_arena_usage(const erd_cache_t* self) {
  return self->arena_offset;
}

/* Returns the arena usage as a percentage (0-100).
 * Useful for monitoring arena utilization. */
static inline uint8_t erd_cache_get_arena_usage_percent(const erd_cache_t* self) {
  return (uint8_t)((self->arena_offset * 100) / ERD_CACHE_ARENA_SIZE);
}

#ifdef __cplusplus
}
#endif

#endif
