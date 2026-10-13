
#include "erd_cache.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include <cstring>

GEA_TAG(TAG) = "erd_cache";

static bool s_slot_overflow_warned = false;
static bool s_arena_overflow_warned = false;
static bool s_size_rejected_warned = false;

/* Acquire the cache lock. No-op if the lock was never created (self->lock is
 * NULL, e.g. before erd_cache_init). Under the test stubs the handle is a
 * non-NULL sentinel and take/give are no-ops, so this is effectively a no-op
 * there too. Blocks until available; the critical section is a short entry
 * read/copy or write, never the MQTT publish. */
static void erd_cache_lock(erd_cache_t* self) {
  if (self->lock) {
    xSemaphoreTake(self->lock, portMAX_DELAY);
  }
}

static void erd_cache_unlock(erd_cache_t* self) {
  if (self->lock) {
    xSemaphoreGive(self->lock);
  }
}

/* Returns true if the new data differs from the existing entry's data.
 * ERD size is invariant after registration, so only memcmp is needed.
 * Uses existing->data_size (not new_size) for the memcmp length to guard
 * against OOB reads if this function is ever called without the size check. */
static bool erd_data_changed(const erd_cache_t* self,
                             const erd_cache_entry_t* existing,
                             const uint8_t* new_data, uint8_t new_size)
{
  (void)new_size; /* Size is invariant; use existing->data_size for safety. */
  const uint8_t* old = &self->arena[existing->data_offset];
  return memcmp(old, new_data, existing->data_size) != 0;
}

/* Reset entries, arena, and counters to the initial state. Does NOT touch the
 * lock (caller manages lock lifecycle). */
static void erd_cache_reset(erd_cache_t* self)
{
  s_slot_overflow_warned = false;
  s_arena_overflow_warned = false;
  s_size_rejected_warned = false;

  /* Zero entries explicitly to avoid UBSan issues with bool fields after memset. */
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    e->erd = 0;
    e->board_address = 0;
    e->data_offset = 0;
    e->update_required = false;
    e->publish_cooldown = 0;
    e->valid = false;
  }
  self->arena_offset = 0;
  memset(self->arena, 0, sizeof(self->arena));
  self->update_count = 0;
  self->update_count_window = 0;
  self->required_update_count = 0;
  self->required_update_count_window = 0;
  self->max_cooldown = 0;
}

void erd_cache_init(erd_cache_t* self)
{
  erd_cache_reset(self);
  if (!self->lock) {
    self->lock = xSemaphoreCreateMutex();
  }
  self->initialized = true;
}

void erd_cache_destroy(erd_cache_t* self)
{
  if (!self->initialized) return;
  if (self->lock) {
    vSemaphoreDelete(self->lock);
    self->lock = NULL;
  }
  erd_cache_reset(self);
  self->initialized = false;
}

static erd_cache_entry_t* erd_cache_find(erd_cache_t* self, tiny_erd_t erd, uint8_t board_address)
{
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->erd == erd && e->board_address == board_address) {
      return e;
    }
  }
  return nullptr;
}

static bool erd_cache_update_locked(erd_cache_t* self, tiny_erd_t erd, uint8_t board_address, const uint8_t* data, uint8_t data_size)
{
  if (data_size > ERD_CACHE_MAX_DATA_SIZE) {
    if (!s_size_rejected_warned) {
      s_size_rejected_warned = true;
      ESP_LOGW(TAG, "ERD 0x%04X rejected: data_size %u exceeds GEA3 max (%u bytes)",
               erd, data_size, ERD_CACHE_MAX_DATA_SIZE);
    }
    return false;
  }

  erd_cache_entry_t* existing = erd_cache_find(self, erd, board_address);

  if (existing) {
    /* Count every cache touch for ERD Publish Rate. */
    self->update_count++;
    self->update_count_window++;

    /* ERD size is invariant after registration.  Check size BEFORE
     * erd_data_changed to avoid reading past the old buffer when the
     * new size is larger. */
    if (data_size != existing->data_size) {
      ESP_LOGE(TAG, "ERD 0x%04X size changed %u -> %u, appliance lost",
               erd, existing->data_size, data_size);
      return false;
    }

    bool data_changed = erd_data_changed(self, existing, data, data_size);

    if (!data_changed) {
      return false;
    }

    memcpy(&self->arena[existing->data_offset], data, data_size);

    existing->update_required = data_changed;
    if (existing->update_required) {
      self->required_update_count++;
      self->required_update_count_window++;
    }
    return existing->update_required;
  }

  erd_cache_entry_t* slot = nullptr;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    if (!self->entries[i].valid) {
      slot = &self->entries[i];
      break;
    }
  }

  if (!slot) {
    if (!s_slot_overflow_warned) {
      s_slot_overflow_warned = true;
      ESP_LOGW(TAG, "ERD cache full (%u slots), new ERD 0x%04X not cached", ERD_CACHE_CAPACITY, erd);
    }
    return false;
  }

  if (self->arena_offset + data_size > ERD_CACHE_ARENA_SIZE) {
    if (!s_arena_overflow_warned) {
      s_arena_overflow_warned = true;
      ESP_LOGW(TAG, "ERD cache arena full (%u bytes), new ERD 0x%04X not cached",
               ERD_CACHE_ARENA_SIZE, erd);
    }
    return false;
  }

  self->update_count++;
  self->update_count_window++;
  self->required_update_count++;
  self->required_update_count_window++;

  slot->data_offset = self->arena_offset;
  memcpy(&self->arena[self->arena_offset], data, data_size);
  self->arena_offset += data_size;

  slot->erd = erd;
  slot->board_address = board_address;
  slot->data_size = data_size;
  slot->valid = true;
  slot->update_required = true;

  ESP_LOGD(TAG, "ERD 0x%04X at address 0x%02X added to cache (%u bytes, arena offset %u)",
           erd, board_address, data_size, slot->data_offset);

  return true;
}

bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, uint8_t board_address, const uint8_t* data, uint8_t data_size)
{
  erd_cache_lock(self);
  bool result = erd_cache_update_locked(self, erd, board_address, data, data_size);
  erd_cache_unlock(self);
  return result;
}

void erd_cache_set_throttle_rate_seconds(erd_cache_t* self, uint8_t rate)
{
  self->max_cooldown = rate;
}

/* Internal (lock-free) get_next_updated. Caller must hold the cache lock. */
static erd_cache_entry_t* erd_cache_get_next_updated_locked(erd_cache_t* self, uint16_t* iterator)
{
  for (uint16_t i = *iterator; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (!e->valid || !e->update_required) continue;

    /* Rate limit: skip if cooldown has not expired. */
    if (self->max_cooldown > 0 && e->publish_cooldown > 0) {
      continue;  /* keep update_required=true, retry next loop */
    }

    /* Eligible — clear flag, return entry.
     * Cooldown reload happens in erd_cache_mark_published() after successful MQTT publish. */
    e->update_required = false;
    *iterator = i + 1;
    return e;
  }
  *iterator = 0; /* Reset iterator for next pass */
  return nullptr;
}

erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator)
{
  erd_cache_lock(self);
  erd_cache_entry_t* entry = erd_cache_get_next_updated_locked(self, iterator);
  erd_cache_unlock(self);
  return entry;
}

/* Atomically fetch the next update_required entry AND copy its payload out of
 * the arena in one lock hold. The arena slice is copied into data_out while the
 * lock is held, so the main loop cannot overwrite it during the slow MQTT publish. */
bool erd_cache_snapshot_next_updated(erd_cache_t* self, uint16_t* iterator,
                                     erd_cache_entry_t** entry_out,
                                     tiny_erd_t* erd_out, uint8_t* addr_out,
                                     uint8_t* data_out, uint8_t* size_out)
{
  erd_cache_lock(self);
  erd_cache_entry_t* entry = erd_cache_get_next_updated_locked(self, iterator);
  if (entry == NULL) {
    erd_cache_unlock(self);
    return false;
  }
  if (data_out != NULL) {
    memcpy(data_out, &self->arena[entry->data_offset], entry->data_size);
  }
  if (entry_out != NULL) *entry_out = entry;
  if (erd_out != NULL) *erd_out = entry->erd;
  if (addr_out != NULL) *addr_out = entry->board_address;
  if (size_out != NULL) *size_out = entry->data_size;
  erd_cache_unlock(self);
  return true;
}

bool erd_cache_snapshot_entry(erd_cache_t* self, tiny_erd_t erd, uint8_t board_address,
                              uint8_t* data_out, uint8_t* size_out)
{
  erd_cache_lock(self);
  erd_cache_entry_t* entry = erd_cache_find(self, erd, board_address);
  if (entry == NULL) {
    erd_cache_unlock(self);
    return false;
  }
  if (data_out != NULL) {
    memcpy(data_out, &self->arena[entry->data_offset], entry->data_size);
  }
  if (size_out != NULL) *size_out = entry->data_size;
  erd_cache_unlock(self);
  return true;
}

uint16_t erd_cache_get_count(erd_cache_t* self)
{
  erd_cache_lock(self);
  uint16_t count = 0;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    if (self->entries[i].valid) {
      count++;
    }
  }
  erd_cache_unlock(self);
  return count;
}

erd_cache_entry_t* erd_cache_get_next_entry(erd_cache_t* self, uint16_t* iterator)
{
  erd_cache_lock(self);
  for (uint16_t i = *iterator; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid) {
      *iterator = i + 1;
      erd_cache_unlock(self);
      return e;
    }
  }
  *iterator = 0; /* Reset iterator for next pass */
  erd_cache_unlock(self);
  return nullptr;
}

uint32_t erd_cache_get_update_rate(erd_cache_t* self)
{
  erd_cache_lock(self);
  uint32_t count = self->update_count_window;
  self->update_count_window = 0;
  erd_cache_unlock(self);
  return count;
}

uint32_t erd_cache_get_required_update_rate(erd_cache_t* self)
{
  erd_cache_lock(self);
  uint32_t count = self->required_update_count_window;
  self->required_update_count_window = 0;
  erd_cache_unlock(self);
  return count;
}

void erd_cache_mark_all_updated(erd_cache_t* self)
{
  erd_cache_lock(self);
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid) {
      e->update_required = true;
      e->publish_cooldown = 0;
    }
  }
  erd_cache_unlock(self);
}

/* Mark an ERD entry as successfully published to MQTT. Takes the cache lock. */
void erd_cache_mark_published(erd_cache_t* self, erd_cache_entry_t* entry)
{
  if (entry == NULL) return;
  erd_cache_lock(self);
  if (self->max_cooldown > 0) {
    entry->publish_cooldown = self->max_cooldown;
  }
  erd_cache_unlock(self);
}

/* Mark an ERD entry as NOT published (publish failed or was dropped).
 * Re-sets update_required so the entry is picked up on the next iteration.
 * Takes the cache lock. */
void erd_cache_mark_unpublished(erd_cache_t* self, erd_cache_entry_t* entry)
{
  if (entry == NULL) return;
  erd_cache_lock(self);
  entry->update_required = true;
  erd_cache_unlock(self);
}

/* Decrement publish_cooldown for all entries with update_required=true.
 * Call once per second from the main loop. Takes the cache lock. */
void erd_cache_tick_cooldowns(erd_cache_t* self)
{
  if (self->max_cooldown == 0) return;
  erd_cache_lock(self);
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->update_required && e->publish_cooldown > 0) {
      e->publish_cooldown--;
    }
  }
  erd_cache_unlock(self);
}

/* RAW accessor: does NOT take the lock. Caller must hold the cache lock
 * (see erd_cache_snapshot_next_updated) or be single-threaded (tests). */
const uint8_t* erd_cache_entry_data(const erd_cache_t* self, const erd_cache_entry_t* entry)
{
  if (entry == NULL || !entry->valid) return NULL;
  return &self->arena[entry->data_offset];
}

/* Returns the number of bytes currently used in the arena.
 * Read-only: arena_offset is a uint16_t (atomic read on ESP32), so no lock is
 * needed. A concurrent writer may bump it by one entry between the read and
 * the return, which is harmless for a monitoring value. */
uint16_t erd_cache_get_arena_usage(const erd_cache_t* self)
{
  return self->arena_offset;
}

/* Returns the arena usage as a percentage (0-100). Read-only, no lock (see above). */
uint8_t erd_cache_get_arena_usage_percent(const erd_cache_t* self)
{
  return (uint8_t)((self->arena_offset * 100) / ERD_CACHE_ARENA_SIZE);
}
