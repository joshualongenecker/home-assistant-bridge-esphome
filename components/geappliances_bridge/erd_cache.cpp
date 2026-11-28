#include "erd_cache.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include <cstring>

GEA_TAG(TAG) = "erd_cache";

static bool s_slot_overflow_warned = false;
static bool s_arena_overflow_warned = false;
static bool s_size_rejected_warned = false;

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

static bool erd_data_changed(const erd_cache_t* self,
                             const erd_cache_entry_t* existing,
                             const uint8_t* new_data, uint8_t new_size)
{
  (void)new_size;
  const uint8_t* old = &self->arena[existing->data_offset];
  return memcmp(old, new_data, existing->data_size) != 0;
}

static void erd_cache_reset(erd_cache_t* self)
{
  s_slot_overflow_warned = false;
  s_arena_overflow_warned = false;
  s_size_rejected_warned = false;

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
  self->dirty_count = 0;
  self->max_cooldown = 0;
}

void erd_cache_clear(erd_cache_t* self)
{
  if (!self->initialized) return;
  erd_cache_lock(self);
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    e->erd = 0;
    e->board_address = 0;
    e->data_offset = 0;
    e->data_size = 0;
    e->update_required = false;
    e->publish_cooldown = 0;
    e->valid = false;
  }
  self->arena_offset = 0;
  memset(self->arena, 0, sizeof(self->arena));
  self->dirty_count = 0;
  erd_cache_unlock(self);
  s_slot_overflow_warned = false;
  s_arena_overflow_warned = false;
  s_size_rejected_warned = false;
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
    self->update_count++;
    self->update_count_window++;

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

    if (data_changed && !existing->update_required) {
      self->dirty_count++;
    }
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
  self->dirty_count++;

  /* VERBOSE, not DEBUG: a populated appliance adds 129-151 entries during the
   * boot window, and every line here is an MQTT publish when the log transport
   * is at its default level. The aggregate lives in the bridge's steady-state
   * summary ("ERDs cached: N, arena: X/Y bytes"). Rebuild at VERBOSE to get the
   * per-entry detail back. */
  ESP_LOGV(TAG, "ERD 0x%04X at address 0x%02X added to cache (%u bytes, arena offset %u)",
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

static erd_cache_entry_t* erd_cache_get_next_updated_locked(erd_cache_t* self, uint16_t* iterator)
{
  for (uint16_t i = *iterator; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (!e->valid || !e->update_required) continue;

    if (self->max_cooldown > 0 && e->publish_cooldown > 0) {
      continue;
    }

    e->update_required = false;
    self->dirty_count--;
    *iterator = i + 1;
    return e;
  }
  *iterator = 0;
  return nullptr;
}

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
  *iterator = 0;
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

uint16_t erd_cache_get_dirty_count(erd_cache_t* self)
{
  erd_cache_lock(self);
  uint16_t count = self->dirty_count;
  erd_cache_unlock(self);
  return count;
}

void erd_cache_mark_all_updated(erd_cache_t* self)
{
  erd_cache_lock(self);
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid) {
      if (!e->update_required) {
        self->dirty_count++;
      }
      e->update_required = true;
      e->publish_cooldown = 0;
    }
  }
  erd_cache_unlock(self);
}

void erd_cache_mark_published(erd_cache_t* self, erd_cache_entry_t* entry)
{
  if (entry == NULL) return;
  erd_cache_lock(self);
  if (entry->valid && self->max_cooldown > 0) {
    entry->publish_cooldown = self->max_cooldown;
  }
  erd_cache_unlock(self);
}

void erd_cache_mark_unpublished(erd_cache_t* self, erd_cache_entry_t* entry)
{
  if (entry == NULL) return;
  erd_cache_lock(self);
  if (!entry->valid) {
    erd_cache_unlock(self);
    return;
  }
  if (!entry->update_required) {
    self->dirty_count++;
  }
  entry->update_required = true;
  erd_cache_unlock(self);
}

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

const uint8_t* erd_cache_entry_data(const erd_cache_t* self, const erd_cache_entry_t* entry)
{
  if (entry == NULL || !entry->valid) return NULL;
  return &self->arena[entry->data_offset];
}

uint16_t erd_cache_get_arena_usage(const erd_cache_t* self)
{
  return self->arena_offset;
}

uint8_t erd_cache_get_arena_usage_percent(const erd_cache_t* self)
{
  return (uint8_t)((self->arena_offset * 100) / ERD_CACHE_ARENA_SIZE);
}
