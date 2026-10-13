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
#define ERD_CACHE_MAX_DATA_SIZE 248    /* GEA3 max payload: 255 - 7 byte overhead */
#define ERD_CACHE_ARENA_SIZE 4096      /* Static arena for all ERD data */

typedef struct {
  tiny_erd_t erd;
  uint8_t board_address;      /* physical board address; the primary host stores its detected address */
  uint16_t data_offset;        /* offset into arena */
  uint8_t data_size;           /* invariant after registration */
  bool update_required;
  uint8_t publish_cooldown;    /* counts down from max_cooldown to 0; 0 = eligible */
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
  SemaphoreHandle_t lock;              /* guards entries[] + arena[] + arena_offset */
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
 *   - ERD size changed (appliance lost)
 */
bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, uint8_t board_address, const uint8_t* data, uint8_t data_size);

/* Set the minimum interval (in seconds) between publishes for any ERD.
 * 0 = disabled (publish on every update). Range: 0-255. */
void erd_cache_set_throttle_rate_seconds(erd_cache_t* self, uint8_t rate);

/* Mark an ERD entry as successfully published to MQTT.
 * Reloads the publish_cooldown timer. Call after mqtt_client_publish_raw() succeeds.
 * Takes the cache lock; safe to call from the publisher task. */
void erd_cache_mark_published(erd_cache_t* self, erd_cache_entry_t* entry);

/* Mark an ERD entry as NOT published (publish failed or was dropped).
 * Re-sets update_required so the entry is picked up on the next iteration.
 * Call when mqtt_client_publish_raw() returns false.
 * Takes the cache lock; safe to call from the publisher task. */
void erd_cache_mark_unpublished(erd_cache_t* self, erd_cache_entry_t* entry);

/* Decrement publish_cooldown for all entries with update_required=true.
 * Call once per second from the main loop. Takes the cache lock. */
void erd_cache_tick_cooldowns(erd_cache_t* self);

/* Returns the next entry with update_required=true, then clears the flag.
 * Caller provides an iterator (uint16_t) initialized to 0.
 * Returns NULL when no more updated entries remain.
 * NOTE: Declared for future use (e.g., batch republish after MQTT reconnect).
 *       Not used in the initial implementation. */
erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator);

/* Atomically fetch the next update_required entry AND copy its payload out of
 * the arena in one lock hold. This is the publisher's safe read path: the
 * arena slice is copied into data_out (capacity >= ERD_CACHE_MAX_DATA_SIZE)
 * while the lock is held, so the main loop cannot overwrite it mid-publish.
 * Returns true and fills the out-params if an entry was found; false otherwise.
 * entry_out receives the stable entry pointer (cache never removes entries) for
 * use with erd_cache_mark_published/unpublished. Other out-params may be NULL. */
bool erd_cache_snapshot_next_updated(erd_cache_t* self, uint16_t* iterator,
                                     erd_cache_entry_t** entry_out,
                                     tiny_erd_t* erd_out, uint8_t* addr_out,
                                     uint8_t* data_out, uint8_t* size_out);

/* Find the entry for (erd, board_address) and copy its payload out of the
 * arena in one lock hold. data_out must have capacity >=
 * ERD_CACHE_MAX_DATA_SIZE. Returns true and fills data_out/size_out if the
 * entry was found and valid; false otherwise (missing or not valid). The lock
 * is held only for the short lookup + copy, never across slow I/O. This is
 * the RMW base-read path: the write bridge snapshots the paired status ERD
 * before patching a field. */
bool erd_cache_snapshot_entry(erd_cache_t* self, tiny_erd_t erd, uint8_t board_address,
                              uint8_t* data_out, uint8_t* size_out);

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
 * RAW accessor: does NOT take the lock. The caller must hold the cache lock
 * (see erd_cache_snapshot_next_updated) or be single-threaded (tests).
 * Returns NULL if entry is NULL or entry is not valid. */
const uint8_t* erd_cache_entry_data(const erd_cache_t* self, const erd_cache_entry_t* entry);

/* Returns the number of bytes currently used in the arena. Read-only, no lock. */
uint16_t erd_cache_get_arena_usage(const erd_cache_t* self);

/* Returns the arena usage as a percentage (0-100). Read-only, no lock. */
uint8_t erd_cache_get_arena_usage_percent(const erd_cache_t* self);

#ifdef __cplusplus
}
#endif

#endif
