/*!
 * @file
 * @brief Unit tests for the ERD cache (erd_cache.h / erd_cache.cpp).
 *
 * Tests cover: init/destroy, arena storage, update flow,
 * change detection, iterators, rate counters,
 * cache full, size change detection.
 *
 * Note: erd_cache_find() is internal (static in .cpp), so tests use
 * erd_cache_get_next_entry() to locate entries by iterating the cache.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTest/MemoryLeakDetectorNewMacros.h"

extern "C" {
#include "erd_cache.h"
}

// Helper: find an entry by iterating the cache (erd_cache_find is internal)
static erd_cache_entry_t* find_entry(erd_cache_t* cache, tiny_erd_t erd)
{
  uint16_t iter = 0;
  erd_cache_entry_t* e;
  while ((e = erd_cache_get_next_entry(cache, &iter)) != NULL) {
    if (e->erd == erd) return e;
  }
  return NULL;
}

TEST_GROUP(erd_cache)
{
  erd_cache_t cache;

  void setup()
  {
    erd_cache_init(&cache);
  }

  void teardown()
  {
    erd_cache_destroy(&cache);
  }
};

TEST(erd_cache, init_zeros_all_entries)
{
  CHECK_TRUE(cache.initialized);
  CHECK_EQUAL(0, cache.update_count);
  CHECK_EQUAL(0, cache.update_count_window);
  CHECK_EQUAL(0, cache.required_update_count);
  CHECK_EQUAL(0, cache.required_update_count_window);
  CHECK_EQUAL(0, erd_cache_get_count(&cache));
}

TEST(erd_cache, destroy_after_init)
{
  erd_cache_destroy(&cache);
  CHECK_FALSE(cache.initialized);
}

TEST(erd_cache, destroy_on_uninitialized_is_safe)
{
  erd_cache_t uninit;
  uninit.initialized = false;
  erd_cache_destroy(&uninit);
}

TEST(erd_cache, insert_1_byte)
{
  uint8_t data[] = { 0xAB };
  bool result = erd_cache_update(&cache, 0x0001, data, 1);
  CHECK_TRUE(result);
  CHECK_EQUAL(1, erd_cache_get_count(&cache));

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_EQUAL(1, entry->data_size);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  CHECK_EQUAL(0xAB, stored[0]);
  CHECK_TRUE(entry->update_required);
  CHECK_TRUE(entry->valid);
}

TEST(erd_cache, insert_4_bytes)
{
  uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
  bool result = erd_cache_update(&cache, 0x0010, data, 4);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0010);
  CHECK(NULL != entry);
  CHECK_EQUAL(4, entry->data_size);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  MEMCMP_EQUAL(data, stored, 4);
}

TEST(erd_cache, insert_5_bytes)
{
  uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
  bool result = erd_cache_update(&cache, 0x0020, data, 5);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0020);
  CHECK(NULL != entry);
  CHECK_EQUAL(5, entry->data_size);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  MEMCMP_EQUAL(data, stored, 5);
}

TEST(erd_cache, insert_large)
{
  uint8_t data[32];
  for (int i = 0; i < 32; i++) data[i] = (uint8_t)i;
  bool result = erd_cache_update(&cache, 0x0100, data, 32);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0100);
  CHECK(NULL != entry);
  CHECK_EQUAL(32, entry->data_size);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  MEMCMP_EQUAL(data, stored, 32);
}

TEST(erd_cache, update_existing_small)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  bool result = erd_cache_update(&cache, 0x0001, data2, 1);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  CHECK_EQUAL(0x02, stored[0]);
  CHECK_TRUE(entry->update_required);
}

TEST(erd_cache, update_existing_medium)
{
  uint8_t data1[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
  uint8_t data2[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
  erd_cache_update(&cache, 0x0020, data1, 5);
  bool result = erd_cache_update(&cache, 0x0020, data2, 5);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0020);
  CHECK(NULL != entry);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  MEMCMP_EQUAL(data2, stored, 5);
}

TEST(erd_cache, size_change_grows_returns_false)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x01, 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  bool result = erd_cache_update(&cache, 0x0001, data2, 2);
  CHECK_FALSE(result);
}

TEST(erd_cache, size_change_shrinks_returns_false)
{
  uint8_t data1[] = { 0x01, 0x02 };
  uint8_t data2[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data1, 2);
  bool result = erd_cache_update(&cache, 0x0001, data2, 1);
  CHECK_FALSE(result);
}

TEST(erd_cache, unchanged_data_returns_false)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  // Unchanged data returns false (early exit)
  bool result = erd_cache_update(&cache, 0x0001, data, 1);
  CHECK_FALSE(result);

  // update_required remains true from the initial insert
  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->update_required);
}
TEST(erd_cache, changed_data_sets_update_required)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);

  bool result = erd_cache_update(&cache, 0x0001, data2, 1);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->update_required);
}
TEST(erd_cache, get_next_updated_returns_updated_entries)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0002, data2, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* e1 = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL != e1);
  CHECK_EQUAL(0x0001, e1->erd);
  CHECK_FALSE(e1->update_required);

  erd_cache_entry_t* e2 = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL != e2);
  CHECK_EQUAL(0x0002, e2->erd);

  erd_cache_entry_t* e3 = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL == e3);
}

TEST(erd_cache, get_next_updated_skips_non_updated)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  uint8_t data3[] = { 0x03 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0002, data2, 1);
  erd_cache_update(&cache, 0x0003, data3, 1);

  erd_cache_entry_t* e2 = find_entry(&cache, 0x0002);
  CHECK(NULL != e2);
  e2->update_required = false;

  uint16_t iter = 0;
  erd_cache_entry_t* e = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL != e);
  CHECK_EQUAL(0x0001, e->erd);

  e = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL != e);
  CHECK_EQUAL(0x0003, e->erd);

  e = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL == e);
}

TEST(erd_cache, get_next_updated_resets_iterator)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_get_next_updated(&cache, &iter);
  erd_cache_get_next_updated(&cache, &iter);
  CHECK_EQUAL(0, iter);
}

TEST(erd_cache, get_next_entry_iterates_all_valid)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0002, data2, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* e1 = erd_cache_get_next_entry(&cache, &iter);
  CHECK(NULL != e1);
  CHECK_EQUAL(0x0001, e1->erd);
  CHECK_TRUE(e1->update_required);

  erd_cache_entry_t* e2 = erd_cache_get_next_entry(&cache, &iter);
  CHECK(NULL != e2);
  CHECK_EQUAL(0x0002, e2->erd);

  erd_cache_entry_t* e3 = erd_cache_get_next_entry(&cache, &iter);
  CHECK(NULL == e3);
}

TEST(erd_cache, get_count_returns_valid_entry_count)
{
  CHECK_EQUAL(0, erd_cache_get_count(&cache));

  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  CHECK_EQUAL(1, erd_cache_get_count(&cache));

  erd_cache_update(&cache, 0x0002, data2, 1);
  CHECK_EQUAL(2, erd_cache_get_count(&cache));
}

TEST(erd_cache, get_update_rate)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_update(&cache, 0x0002, data, 1);

  CHECK_EQUAL(2, erd_cache_get_update_rate(&cache));
  CHECK_EQUAL(0, erd_cache_get_update_rate(&cache));

  erd_cache_update(&cache, 0x0003, data, 1);
  CHECK_EQUAL(1, erd_cache_get_update_rate(&cache));
}

TEST(erd_cache, get_required_update_rate)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_update(&cache, 0x0002, data, 1);

  CHECK_EQUAL(2, erd_cache_get_required_update_rate(&cache));
  CHECK_EQUAL(0, erd_cache_get_required_update_rate(&cache));
}

TEST(erd_cache, required_update_rate_with_unchanged_data)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0001, data1, 1);

  CHECK_EQUAL(1, erd_cache_get_required_update_rate(&cache));

  erd_cache_update(&cache, 0x0001, data2, 1);
  CHECK_EQUAL(1, erd_cache_get_required_update_rate(&cache));
}

TEST(erd_cache, cache_full_rejects_new_erd)
{
  for (int i = 0; i < ERD_CACHE_CAPACITY; i++) {
    uint8_t data[] = { (uint8_t)i };
    tiny_erd_t erd = (tiny_erd_t)(0x0001 + i);
    bool result = erd_cache_update(&cache, erd, data, 1);
    CHECK_TRUE(result);
  }
  CHECK_EQUAL(ERD_CACHE_CAPACITY, erd_cache_get_count(&cache));

  uint8_t data[] = { 0xFF };
  bool result = erd_cache_update(&cache, 0xFFFF, data, 1);
  CHECK_FALSE(result);
}

TEST(erd_cache, find_returns_null_for_unknown_erd)
{
  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL == entry);
}

TEST(erd_cache, destroy_clears_arena)
{
  uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
  erd_cache_update(&cache, 0x0020, data, 5);

  CHECK_EQUAL(5, erd_cache_get_arena_usage(&cache));

  erd_cache_destroy(&cache);
  CHECK_FALSE(cache.initialized);
  CHECK_EQUAL(0, erd_cache_get_arena_usage(&cache));
}

TEST(erd_cache, reinit_after_destroy)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_destroy(&cache);

  erd_cache_init(&cache);
  CHECK_EQUAL(0, erd_cache_get_count(&cache));

  erd_cache_update(&cache, 0x0002, data, 1);
  CHECK_EQUAL(1, erd_cache_get_count(&cache));
}

TEST(erd_cache, new_entry_always_sets_update_required)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->update_required);
}

TEST(erd_cache, get_next_entry_resets_iterator_on_exhaust)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_get_next_entry(&cache, &iter);
  erd_cache_get_next_entry(&cache, &iter);
  CHECK_EQUAL(0, iter);

  erd_cache_entry_t* e = erd_cache_get_next_entry(&cache, &iter);
  CHECK(NULL != e);
  CHECK_EQUAL(0x0001, e->erd);
}

TEST(erd_cache, update_count_increments_on_existing_update)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0001, data2, 1);

  CHECK_EQUAL(2, cache.update_count);
}

TEST(erd_cache, update_count_increments_on_new_entry)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_update(&cache, 0x0002, data, 1);
  erd_cache_update(&cache, 0x0003, data, 1);

  CHECK_EQUAL(3, cache.update_count);
}

TEST(erd_cache, unchanged_data_does_not_increment_required_count)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_update(&cache, 0x0001, data, 1);

  CHECK_EQUAL(1, cache.required_update_count);
}

/* --- Rate limiting tests --- */

TEST(erd_cache, rate_limit_disabled_allows_immediate_publish)
{
  /* max_cooldown=0 (default) — every update publishes immediately. */
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(0x0001, entry->erd);
}

TEST(erd_cache, rate_limit_first_entry_publishes_immediately)
{
  /* New entries always publish immediately, even with rate limiting enabled. */
  erd_cache_set_throttle_rate_seconds(&cache, 5);
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(0x0001, entry->erd);

  /* After publish, cooldown is reloaded to max_cooldown. */
  erd_cache_mark_published(&cache, entry);
  iter = 0;
  entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(5, entry->publish_cooldown);
}

TEST(erd_cache, rate_limit_blocks_republish_until_cooldown_expires)
{
  erd_cache_set_throttle_rate_seconds(&cache, 5);
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  /* First publish — immediate. */
  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  erd_cache_mark_published(&cache, entry);

  /* Update again — should be blocked by cooldown. */
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data2, 1);

  /* get_next_updated should skip the rate-limited entry. */
  iter = 0;
  CHECK(NULL == erd_cache_get_next_updated(&cache, &iter));

  /* Tick 4 times — cooldown goes from 5 to 1, still blocked. */
  for (int i = 0; i < 4; i++) {
    erd_cache_tick_cooldowns(&cache);
  }
  iter = 0;
  entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(1, entry->publish_cooldown);
  CHECK(NULL == erd_cache_get_next_updated(&cache, &iter));

  /* Tick once more — cooldown reaches 0, now eligible. */
  erd_cache_tick_cooldowns(&cache);
  iter = 0;
  entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(0, entry->publish_cooldown);

  iter = 0;
  entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(0x0001, entry->erd);
}

TEST(erd_cache, rate_limit_reload_after_publish)
{
  erd_cache_set_throttle_rate_seconds(&cache, 3);
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  erd_cache_mark_published(&cache, entry);

  uint16_t iter2 = 0;
  entry = erd_cache_get_next_entry(&cache, &iter2);
  CHECK(entry != NULL);
  CHECK_EQUAL(3, entry->publish_cooldown);
}

TEST(erd_cache, rate_limit_tick_only_decrements_when_update_required)
{
  erd_cache_set_throttle_rate_seconds(&cache, 5);
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  erd_cache_mark_published(&cache, entry);

  /* Cooldown is 5, but update_required is false — tick should not decrement. */
  erd_cache_tick_cooldowns(&cache);
  iter = 0;
  entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(5, entry->publish_cooldown);  /* unchanged */
}

TEST(erd_cache, rate_limit_multiple_erds_independent)
{
  erd_cache_set_throttle_rate_seconds(&cache, 3);
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0002, data2, 1);

  /* Both publish immediately (first time). */
  uint16_t iter = 0;
  erd_cache_entry_t* e1 = erd_cache_get_next_updated(&cache, &iter);
  CHECK(e1 != NULL);
  erd_cache_mark_published(&cache, e1);

  erd_cache_entry_t* e2 = erd_cache_get_next_updated(&cache, &iter);
  CHECK(e2 != NULL);
  erd_cache_mark_published(&cache, e2);

  /* Update only ERD 0x0001. */
  uint8_t data3[] = { 0x03 };
  erd_cache_update(&cache, 0x0001, data3, 1);

  /* 0x0001 is rate-limited, 0x0002 has no pending update. */
  iter = 0;
  CHECK(NULL == erd_cache_get_next_updated(&cache, &iter));

  /* Tick twice — 0x0001 cooldown goes from 3 to 1. */
  erd_cache_tick_cooldowns(&cache);
  erd_cache_tick_cooldowns(&cache);

  /* Update ERD 0x0002 — should publish immediately (cooldown was 3, tick brought it to 1, but update_required was false so it stayed at 3... wait, tick only decrements when update_required=true). */
  /* Actually 0x0002 has update_required=false, so its cooldown stayed at 3.
   * But new updates set update_required=true, and the cooldown check is > 0.
   * So 0x0002 should also be blocked. Let me reconsider... */
  /* The cooldown for 0x0002 is still 3 (tick skipped it because update_required was false).
   * Now we update it, setting update_required=true. Its cooldown is 3 > 0, so it's blocked. */
  uint8_t data4[] = { 0x04 };
  erd_cache_update(&cache, 0x0002, data4, 1);
  iter = 0;
  CHECK(NULL == erd_cache_get_next_updated(&cache, &iter));
}

TEST(erd_cache, rate_limit_disabled_via_zero)
{
  erd_cache_set_throttle_rate_seconds(&cache, 0);
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  erd_cache_mark_published(&cache, entry);

  /* With max_cooldown=0, mark_published sets cooldown to 0. */
  iter = 0;
  entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(0, entry->publish_cooldown);

  /* Update again — should publish immediately. */
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data2, 1);
  iter = 0;
  entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
}

TEST(erd_cache, rate_limit_tick_noop_when_disabled)
{
  erd_cache_set_throttle_rate_seconds(&cache, 0);
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  erd_cache_mark_published(&cache, entry);

  /* Tick should be a no-op when max_cooldown=0. */
  erd_cache_tick_cooldowns(&cache);
  iter = 0;
  entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(0, entry->publish_cooldown);
}
/* Rate limiting: unchanged data does not reset cooldown */
TEST(erd_cache, rate_limit_unchanged_data_does_not_reset_cooldown)
{
  erd_cache_set_throttle_rate_seconds(&cache, 5);
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  /* First publish — immediate (new entry). */
  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  erd_cache_mark_published(&cache, entry);
  /* Send the same data again — unchanged data is skipped. */
  erd_cache_update(&cache, 0x0001, data, 1);

  /* update_required should not have been set, so cooldown is untouched. */
  iter = 0;
  entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(5, entry->publish_cooldown);  /* still 5, unchanged */
  CHECK(!entry->update_required);

  /* Tick 4 times — cooldown stays at 5 (update_required=false, tick skips). */
  for (int i = 0; i < 4; i++) {
    erd_cache_tick_cooldowns(&cache);
  }
  iter = 0;
  entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(5, entry->publish_cooldown);  /* unchanged — tick skips when !update_required */

  /* Now send different data — update_required is set, cooldown still 5, blocked. */
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data2, 1);
  iter = 0;
  CHECK(NULL == erd_cache_get_next_updated(&cache, &iter));

  /* Tick 5 times — cooldown reaches 0, now eligible. */
  for (int i = 0; i < 5; i++) {
    erd_cache_tick_cooldowns(&cache);
  }
  iter = 0;
  entry = erd_cache_get_next_updated(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(0x0001, entry->erd);
}

/* New test: ERD exceeding GEA3 max size is rejected */
TEST(erd_cache, insert_rejected_over_248_bytes)
{
  uint8_t data[249];
  memset(data, 0xAA, sizeof(data));
  bool result = erd_cache_update(&cache, 0x0100, data, 249);
  CHECK_FALSE(result);
  CHECK_EQUAL(0, erd_cache_get_count(&cache));
}

/* New test: ERD at exactly 248 bytes is accepted */
TEST(erd_cache, insert_accepted_at_248_bytes)
{
  uint8_t data[248];
  memset(data, 0xBB, sizeof(data));
  bool result = erd_cache_update(&cache, 0x0100, data, 248);
  CHECK_TRUE(result);
  CHECK_EQUAL(1, erd_cache_get_count(&cache));

  erd_cache_entry_t* entry = find_entry(&cache, 0x0100);
  CHECK(NULL != entry);
  CHECK_EQUAL(248, entry->data_size);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  MEMCMP_EQUAL(data, stored, 248);
}

/* New test: Arena full rejects new ERD */
TEST(erd_cache, arena_full_rejects_new_erd)
{
  constexpr uint8_t entry_size = 248;
  const uint16_t entries_to_fill = ERD_CACHE_ARENA_SIZE / entry_size;
  /* Fill every complete 248-byte slot in the configured arena. */
  for (uint16_t i = 0; i < entries_to_fill; i++) {
    uint8_t data[248];
    memset(data, (uint8_t)i, sizeof(data));
    tiny_erd_t erd = (tiny_erd_t)(0x0001 + i);
    bool result = erd_cache_update(&cache, erd, data, entry_size);
    CHECK_TRUE(result);
  }
  CHECK_EQUAL(entries_to_fill, erd_cache_get_count(&cache));

  /* One more full-size ERD exceeds the configured arena. */
  uint8_t data[248];
  memset(data, 0xFF, sizeof(data));
  bool result = erd_cache_update(&cache, 0xFFFF, data, entry_size);
  CHECK_FALSE(result);
  CHECK_EQUAL(entries_to_fill, erd_cache_get_count(&cache));
}

/* New test: Arena usage tracking */
TEST(erd_cache, arena_usage_tracking)
{
  CHECK_EQUAL(0, erd_cache_get_arena_usage(&cache));
  CHECK_EQUAL(0, erd_cache_get_arena_usage_percent(&cache));

  /* Add 100 bytes */
  uint8_t data[100];
  memset(data, 0xAA, sizeof(data));
  erd_cache_update(&cache, 0x0001, data, 100);

  CHECK_EQUAL(100, erd_cache_get_arena_usage(&cache));
  CHECK_EQUAL((100 * 100) / ERD_CACHE_ARENA_SIZE, erd_cache_get_arena_usage_percent(&cache));

  /* Add another 50 bytes */
  uint8_t data2[50];
  memset(data2, 0xBB, sizeof(data2));
  erd_cache_update(&cache, 0x0002, data2, 50);

  CHECK_EQUAL(150, erd_cache_get_arena_usage(&cache));
  CHECK_EQUAL((150 * 100) / ERD_CACHE_ARENA_SIZE, erd_cache_get_arena_usage_percent(&cache));
}
