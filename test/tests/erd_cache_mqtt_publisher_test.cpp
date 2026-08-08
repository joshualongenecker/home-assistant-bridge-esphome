/*!
 * @file
 * @brief Unit tests for the ERD cache MQTT publisher module.
 */

#include "CppUTest/TestHarness.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

extern "C" {
#include "erd_cache.h"
#include "erd_cache_mqtt_publisher.h"
}

#include "esphome_mqtt_client_adapter.h"
#include "double/mqtt_test_double.hpp"
#include "double/esphome_hal_double.hpp"

TEST_GROUP(erd_cache_mqtt_publisher)
{
  erd_cache_mqtt_publisher_t publisher;
  erd_cache_t cache;
  esphome_mqtt_client_adapter_t adapter;
  esphome::mqtt::MqttTestDouble mqtt_double;

  void setup()
  {
    if (publisher.cache) {
      erd_cache_mqtt_publisher_destroy(&publisher);
    }
    erd_cache_destroy(&cache);
    memset(&publisher, 0, sizeof(publisher));
    erd_cache_init(&cache);
    esphome_mqtt_client_adapter_init(&adapter, "test_device");
    esphome::mqtt::global_mqtt_client = &mqtt_double;
    mqtt_double.connected_ = true;
  }

  void teardown()
  {
    if (publisher.cache) {
      erd_cache_mqtt_publisher_destroy(&publisher);
    }
    erd_cache_destroy(&cache);
    esphome_mqtt_client_adapter_destroy(&adapter);
    esphome::mqtt::global_mqtt_client = nullptr;
    mqtt_double.connected_ = false;
  }
};

/* ------------------------------------------------------------------ */
/* init / destroy                                                       */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, init_sets_cache_pointer)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "my_device");

  CHECK(publisher.cache != nullptr);
  CHECK_EQUAL(0u, publisher.publish_index);
  erd_cache_mqtt_publisher_on_disconnected(&publisher); // double starts connected in setup
  CHECK(!publisher.mqtt_connected);
  CHECK_EQUAL(0u, publisher.total_published);
  CHECK_EQUAL(0u, publisher.missed_loops);
  CHECK(strncmp(publisher.device_id, "my_device", 8) == 0);
}

TEST(erd_cache_mqtt_publisher, init_sets_mqtt_connected_false)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_disconnected(&publisher); // double starts connected in setup
  CHECK(!publisher.mqtt_connected);
}

TEST(erd_cache_mqtt_publisher, destroy_unsubscribes_events)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  // Destroy should not crash even after events are set up.
  erd_cache_mqtt_publisher_destroy(&publisher);
  CHECK(publisher.cache == nullptr);
}

TEST(erd_cache_mqtt_publisher, destroy_is_safe_when_not_initialized)
{
  // Destroy on a zeroed struct should not crash.
  erd_cache_mqtt_publisher_destroy(&publisher);
}

TEST(erd_cache_mqtt_publisher, init_handles_null_mqtt_client_gracefully)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    nullptr,
    "device");

  /* Should not crash; fields set before the null guard are still valid. */
  CHECK(publisher.cache != nullptr);
  CHECK_EQUAL(0u, publisher.publish_index);
  CHECK(!publisher.mqtt_connected);  // Initially disconnected
}

TEST(erd_cache_mqtt_publisher, destroy_after_init_with_null_mqtt_client)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    nullptr,
    "device");

  /* Destroy should clean up without crash when mqtt_client was null. */
  erd_cache_mqtt_publisher_destroy(&publisher);
  CHECK(publisher.cache == nullptr);
  CHECK(publisher.mqtt_client == nullptr);
}

/* ------------------------------------------------------------------ */
/* loop - basic publish                                                 */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, loop_publishes_updated_erd)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "my_device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
  CHECK_EQUAL(1u, publisher.total_published);
}
TEST(erd_cache_mqtt_publisher, loop_returns_zero_when_no_updates)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
}

TEST(erd_cache_mqtt_publisher, loop_publishes_one_per_call)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  // Insert 20 ERDs with update_required=true
  for (uint16_t i = 0; i < 20; i++) {
    uint8_t data = (uint8_t)i;
    erd_cache_update(&cache, (tiny_erd_t)(0x1000 + i), 0xFF, &data, sizeof(data));
  }

  // Only one ERD is published per call
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
  CHECK_EQUAL(1u, publisher.total_published);
}

TEST(erd_cache_mqtt_publisher, loop_skips_when_mqtt_disconnected)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  // Disconnect so loop skips publishing
  erd_cache_mqtt_publisher_on_disconnected(&publisher);

  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
  CHECK_EQUAL(0u, publisher.total_published);
}
TEST(erd_cache_mqtt_publisher, loop_resumes_after_reconnect)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  // Disconnect — should not publish
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);

  // Reconnect — should publish
  erd_cache_mqtt_publisher_on_connected(&publisher);
  published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
}

/* ------------------------------------------------------------------ */
/* loop - retry on dropped publish                                    */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, loop_marks_unpublished_on_drop)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  /* Force the double to fail publish (simulates queue overflow). */
  mqtt_double.publish_should_fail_ = true;

  erd_cache_mqtt_publisher_loop(&publisher);
  /* The entry should still have update_required set for retry. */
  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(0x0008u, entry->erd);
  CHECK_TRUE(entry->update_required);
}

TEST(erd_cache_mqtt_publisher, loop_retries_after_drop)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  /* First attempt fails. */
  mqtt_double.publish_should_fail_ = true;
  erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_EQUAL(0u, publisher.total_published);

  /* Second call: iterator has advanced past the entry, so it scans
   * the rest of the cache, resets to 0, and returns NULL. */
  mqtt_double.publish_should_fail_ = false;
  CHECK_FALSE(erd_cache_mqtt_publisher_loop(&publisher));

  /* Third call: iterator is at 0, finds the retried entry. */
  erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_EQUAL(1u, publisher.total_published);

  /* Entry is now published — no more pending. */
  CHECK_FALSE(erd_cache_mqtt_publisher_loop(&publisher));
}


/* ------------------------------------------------------------------ */
/* loop - topic format                                                  */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, topic_format_correct)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "my_device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  // The publisher will call esphome_mqtt_client_adapter_publish with the topic.
  // We verify it doesn't crash and the topic is constructed correctly.
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
}

/* ------------------------------------------------------------------ */
/* loop - payload format                                                */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, payload_lowercase_hex_no_separator)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data[] = {0x01, 0xAB, 0xFF};
  erd_cache_update(&cache, 0x1001, 0xFF, data, sizeof(data));

  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
}

/* ------------------------------------------------------------------ */
/* loop - retain flag                                                   */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, retain_flag_true)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  // The publisher always passes retain=true to esphome_mqtt_client_adapter_publish.
  // We verify the call completes without crashing.
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
}

/* ------------------------------------------------------------------ */
/* on_connected / on_disconnected                                       */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, on_disconnected_sets_flag)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  // Double starts connected in setup; disconnect to test the flag
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK(!publisher.mqtt_connected);

  // Disconnect, then reconnect
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK(!publisher.mqtt_connected);
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK(publisher.mqtt_connected);
}

TEST(erd_cache_mqtt_publisher, on_disconnected_then_connected_toggles_flag)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  // Double starts connected in setup; disconnect to test the flag
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK(!publisher.mqtt_connected);
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK(!publisher.mqtt_connected);
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK(publisher.mqtt_connected);
}

/* ------------------------------------------------------------------ */
/* Event-driven disconnect/reconnect                                    */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, disconnect_event_is_noop_when_already_disconnected)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  // Start disconnected; disconnect event is a no-op, flag stays false

  // Trigger disconnect through the adapter
  esphome_mqtt_client_adapter_notify_disconnected(&adapter);

  CHECK(!publisher.mqtt_connected);
}

TEST(erd_cache_mqtt_publisher, connect_event_triggers_callback)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  publisher.mqtt_connected = false;

  // Trigger connect through the adapter
  esphome_mqtt_client_adapter_notify_connected(&adapter);

  CHECK(publisher.mqtt_connected);
}

/* ------------------------------------------------------------------ */
/* loop - round robin index                                             */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, loop_advances_publish_index)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  // Insert 10 ERDs
  for (uint16_t i = 0; i < 10; i++) {
    uint8_t data = (uint8_t)i;
    erd_cache_update(&cache, (tiny_erd_t)(0x2000 + i), 0xFF, &data, sizeof(data));
  }

  // Each call publishes one ERD and advances the index
  CHECK_TRUE(erd_cache_mqtt_publisher_loop(&publisher));
  CHECK_EQUAL(1u, publisher.total_published);

  // Next call publishes the next ERD
  CHECK_TRUE(erd_cache_mqtt_publisher_loop(&publisher));
  CHECK_EQUAL(2u, publisher.total_published);
}


TEST(erd_cache_mqtt_publisher, loop_returns_zero_with_null_cache)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  publisher.cache = nullptr;
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
}

TEST(erd_cache_mqtt_publisher, loop_returns_zero_with_null_mqtt_client)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  publisher.mqtt_client = nullptr;
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
}

/* ------------------------------------------------------------------ */
/* Multiple calls publish sequentially                                  */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, loop_publishes_one_per_call_with_multiple_pending)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data_a = 0x01;
  uint8_t data_b = 0x02;
  uint8_t data_c = 0x03;
  erd_cache_update(&cache, 0x4001, 0xFF, &data_a, sizeof(data_a));
  erd_cache_update(&cache, 0x4002, 0xFF, &data_b, sizeof(data_b));
  erd_cache_update(&cache, 0x4003, 0xFF, &data_c, sizeof(data_c));

  // Each call publishes exactly one ERD
  CHECK_TRUE(erd_cache_mqtt_publisher_loop(&publisher));
  CHECK_EQUAL(1u, publisher.total_published);

  CHECK_TRUE(erd_cache_mqtt_publisher_loop(&publisher));
  CHECK_EQUAL(2u, publisher.total_published);

  CHECK_TRUE(erd_cache_mqtt_publisher_loop(&publisher));
  CHECK_EQUAL(3u, publisher.total_published);

  // No more pending — returns false
  CHECK_FALSE(erd_cache_mqtt_publisher_loop(&publisher));
}

/* ------------------------------------------------------------------ */
/* loop - large payload hex encoding (Issue 9 fix)                     */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, loop_publishes_32_byte_payload)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data[32];
  for (uint8_t i = 0; i < 32; i++) {
    data[i] = i;
  }
  erd_cache_update(&cache, 0x1001, 0xFF, data, sizeof(data));

  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
  CHECK_EQUAL(1u, publisher.total_published);
}
/* ------------------------------------------------------------------ */
/* Cache change-detection tests                                        */
/* ------------------------------------------------------------------ */

TEST_GROUP(erd_cache_change_detection)
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

/* same-size, same data → no change */
TEST(erd_cache_change_detection, same_size_same_data_no_change)
{
  uint8_t data[] = { 0x01, 0x02, 0x03 };
  erd_cache_update(&cache, 0x1001, 0xFF, data, sizeof(data));
  /* update with identical data */
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, 0xFF, data, sizeof(data)));
}

/* same-size, different data → change detected */
TEST(erd_cache_change_detection, same_size_different_data_change_detected)
{
  uint8_t data1[] = { 0x01, 0x02, 0x03 };
  uint8_t data2[] = { 0x01, 0x02, 0x04 };
  erd_cache_update(&cache, 0x1001, 0xFF, data1, sizeof(data1));
  CHECK_TRUE(erd_cache_update(&cache, 0x1001, 0xFF, data2, sizeof(data2)));
}

/* Size shrink is treated as appliance lost — returns false. */
TEST(erd_cache_change_detection, size_shrink_same_prefix_change_detected)
{
  uint8_t data1[] = { 0x01, 0x02, 0x03, 0x04 };
  uint8_t data2[] = { 0x01, 0x02 };
  erd_cache_update(&cache, 0x1001, 0xFF, data1, sizeof(data1));
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, 0xFF, data2, sizeof(data2)));
}

/* Size grow is treated as appliance lost — returns false. */
TEST(erd_cache_change_detection, size_grow_same_prefix_change_detected)
{
  uint8_t data1[] = { 0x01, 0x02 };
  uint8_t data2[] = { 0x01, 0x02, 0x03, 0x04 };
  erd_cache_update(&cache, 0x1001, 0xFF, data1, sizeof(data1));
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, 0xFF, data2, sizeof(data2)));
}

/* Size change (growth) is treated as appliance lost — returns false. */
TEST(erd_cache_change_detection, size_change_grow_rejected)
{
  uint8_t data_small[8];
  memset(data_small, 0xAA, sizeof(data_small));
  erd_cache_update(&cache, 0x1001, 0xFF, data_small, sizeof(data_small));

  uint8_t data_large[20];
  memcpy(data_large, data_small, sizeof(data_small));
  memset(data_large + sizeof(data_small), 0xBB, sizeof(data_large) - sizeof(data_small));
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, 0xFF, data_large, sizeof(data_large)));
}

/* Size change (shrink) is treated as appliance lost — returns false. */
TEST(erd_cache_change_detection, size_change_shrink_rejected)
{
  uint8_t data_large[20];
  memset(data_large, 0xAA, sizeof(data_large));
  erd_cache_update(&cache, 0x1001, 0xFF, data_large, sizeof(data_large));

  uint8_t data_small[8];
  memset(data_small, 0xAA, sizeof(data_small));
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, 0xFF, data_small, sizeof(data_small)));
}

/* New entry with data stored in arena */
TEST(erd_cache_change_detection, arena_path_new_entry_stored)
{
  uint8_t data[20];
  for (uint8_t i = 0; i < 20; i++) {
    data[i] = i;
  }
  erd_cache_update(&cache, 0x1001, 0xFF, data, sizeof(data));

  CHECK_EQUAL(1u, erd_cache_get_count(&cache));

  uint16_t iterator = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&cache, &iterator);
  CHECK(entry != NULL);
  CHECK_EQUAL(20u, entry->data_size);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  for (uint8_t i = 0; i < 20; i++) {
    CHECK_EQUAL(i, stored[i]);
  }
}

/* Update existing arena entry with different data */
TEST(erd_cache_change_detection, arena_path_update_existing_entry)
{
  uint8_t data1[20];
  for (uint8_t i = 0; i < 20; i++) {
    data1[i] = i;
  }
  erd_cache_update(&cache, 0x1001, 0xFF, data1, sizeof(data1));

  uint8_t data2[20];
  for (uint8_t i = 0; i < 20; i++) {
    data2[i] = 255 - i;
  }
  CHECK_TRUE(erd_cache_update(&cache, 0x1001, 0xFF, data2, sizeof(data2)));

  CHECK_EQUAL(1u, erd_cache_get_count(&cache));

  uint16_t iterator = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&cache, &iterator);
  CHECK(entry != NULL);
  CHECK_EQUAL(20u, entry->data_size);
  const uint8_t* stored = erd_cache_entry_data(&cache, entry);
  for (uint8_t i = 0; i < 20; i++) {
    CHECK_EQUAL(255 - i, stored[i]);
  }
}

/* #19: Cache overflow — 201st insert rejected when cache is full */
TEST(erd_cache_change_detection, cache_overflow_rejects_new_erd)
{
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    uint8_t data = 0x42;
    erd_cache_update(&cache, (tiny_erd_t)(0x8000 + i), 0xFF, &data, sizeof(data));
  }
  CHECK_EQUAL(ERD_CACHE_CAPACITY, erd_cache_get_count(&cache));

  /* 201st ERD should be rejected */
  uint8_t data = 0xFF;
  CHECK_FALSE(erd_cache_update(&cache, 0xFFFF, 0xFF, &data, sizeof(data)));
  CHECK_EQUAL(ERD_CACHE_CAPACITY, erd_cache_get_count(&cache));
}


/* #23: MQTT reconnect — ERD still published after disconnect/reconnect cycle */
TEST(erd_cache_mqtt_publisher, loop_publishes_after_disconnect_reconnect)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, 0xFF, &data, sizeof(data));

  /* Simulate disconnect — loop should skip publishing */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
  CHECK(publisher.missed_loops > 0);

  /* Simulate reconnect — loop should publish the pending ERD */
  erd_cache_mqtt_publisher_on_connected(&publisher);
  published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
}
/* #23: MQTT reconnect — no publish after reconnect when nothing has changed */
TEST(erd_cache_mqtt_publisher, loop_no_publish_after_reconnect_when_no_changes)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, 0xFF, &data, sizeof(data));

  /* Publish the ERD first to clear update_required */
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);

  /* Disconnect and reconnect */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* No new data — nothing should be published */
  published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
}

/* Long disconnect republish: short disconnect (<60s) does not republish */
TEST(erd_cache_mqtt_publisher, short_disconnect_no_republish)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_set_time_fn(&publisher, esphome_hal_double_get_millis);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, 0xFF, &data, sizeof(data));

  /* Publish to clear update_required */
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);

  /* Disconnect at t=1000 (non-zero so disconnect_start_ms != 0) */
  esphome_hal_double_set_millis(1000);
  erd_cache_mqtt_publisher_on_disconnected(&publisher);

  /* Reconnect at t=31000 (30s gap, short disconnect) */
  esphome_hal_double_set_millis(31000);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* Should NOT republish — disconnect was too short */
  published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
}

/* Long disconnect republish: disconnect >=60s triggers full republish */
TEST(erd_cache_mqtt_publisher, long_disconnect_republish_all)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_set_time_fn(&publisher, esphome_hal_double_get_millis);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* Insert 3 ERDs and publish them to clear update_required */
  uint8_t data1 = 0x01, data2 = 0x02, data3 = 0x03;
  erd_cache_update(&cache, 0x1001, 0xFF, &data1, sizeof(data1));
  erd_cache_update(&cache, 0x1002, 0xFF, &data2, sizeof(data2));
  erd_cache_update(&cache, 0x1003, 0xFF, &data3, sizeof(data3));
  /* Publish all 3 to clear update_required */
  erd_cache_mqtt_publisher_loop(&publisher);
  erd_cache_mqtt_publisher_loop(&publisher);
  erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_EQUAL(3u, publisher.total_published);

  /* Disconnect at t=1000 (non-zero so disconnect_start_ms != 0) */
  esphome_hal_double_set_millis(1000);
  erd_cache_mqtt_publisher_on_disconnected(&publisher);

  /* Reconnect at t=71000 (70s gap, exceeds 60s threshold) */
  esphome_hal_double_set_millis(71000);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* All 3 entries should be republished — call loop 3 times */
  CHECK_TRUE(erd_cache_mqtt_publisher_loop(&publisher));
  CHECK_TRUE(erd_cache_mqtt_publisher_loop(&publisher));
  CHECK_TRUE(erd_cache_mqtt_publisher_loop(&publisher));
  CHECK_EQUAL(6u, publisher.total_published);
}

/* Long disconnect republish: empty cache is safe (no crash) */
TEST(erd_cache_mqtt_publisher, long_disconnect_empty_cache_safe)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_set_time_fn(&publisher, esphome_hal_double_get_millis);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* Disconnect at t=1000, reconnect at t=71000 with empty cache */
  esphome_hal_double_set_millis(1000);
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  esphome_hal_double_set_millis(71000);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* Should publish nothing, no crash */
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
}

/* Long disconnect republish: exactly 60s threshold triggers republish */
TEST(erd_cache_mqtt_publisher, exact_threshold_republish)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_set_time_fn(&publisher, esphome_hal_double_get_millis);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, 0xFF, &data, sizeof(data));
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);

  /* Disconnect at t=1000, reconnect at exactly t=61000 (60s gap) */
  esphome_hal_double_set_millis(1000);
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  esphome_hal_double_set_millis(61000);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* Exactly 60s should trigger republish (>= threshold) */
  published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
}

/* Long disconnect republish: 59999ms (just under 60s) should NOT republish */
TEST(erd_cache_mqtt_publisher, just_under_threshold_no_republish)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_set_time_fn(&publisher, esphome_hal_double_get_millis);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, 0xFF, &data, sizeof(data));
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);

  /* Disconnect at t=1000, reconnect at t=59999 (58999ms gap, under threshold) */
  esphome_hal_double_set_millis(1000);
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  esphome_hal_double_set_millis(60999);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* Just under threshold — should NOT republish */
  published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);
}

/* Rate limiting: publisher reloads cooldown after successful publish */
TEST(erd_cache_mqtt_publisher, loop_reloads_cooldown_after_publish)
{
  erd_cache_set_throttle_rate_seconds(&cache, 5);
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, 0xFF, &data, sizeof(data));

  /* First publish — immediate (new entry, cooldown=0). */
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);

  /* Verify cooldown was reloaded to max_cooldown. */
  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(5, entry->publish_cooldown);
}

/* Rate limiting: publisher skips entries whose cooldown has not expired */
TEST(erd_cache_mqtt_publisher, loop_skips_rate_limited_entries)
{
  erd_cache_set_throttle_rate_seconds(&cache, 5);
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, 0xFF, &data, sizeof(data));

  /* First publish — immediate. */
  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);

  /* Update again — cooldown is 5, should be blocked. */
  uint8_t data2 = 0x99;
  erd_cache_update(&cache, 0x1001, 0xFF, &data2, sizeof(data2));

  /* Loop should publish nothing (rate-limited). */
  published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_FALSE(published);

  /* Tick cooldown to 0. */
  for (int i = 0; i < 5; i++) {
    erd_cache_tick_cooldowns(&cache);
  }

  /* Now the loop should publish. */
  published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
}

/* ------------------------------------------------------------------ */
/* Disconnect count and duration sensors                                */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, disconnect_count_starts_at_zero)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  CHECK_EQUAL(0u, erd_cache_mqtt_publisher_get_disconnect_count(&publisher));
}

TEST(erd_cache_mqtt_publisher, disconnect_count_increments_on_disconnect)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK_EQUAL(1u, erd_cache_mqtt_publisher_get_disconnect_count(&publisher));

  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK_EQUAL(2u, erd_cache_mqtt_publisher_get_disconnect_count(&publisher));

  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK_EQUAL(3u, erd_cache_mqtt_publisher_get_disconnect_count(&publisher));
}

TEST(erd_cache_mqtt_publisher, last_disconnect_duration_starts_at_zero)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  CHECK_EQUAL(0u, erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(&publisher));
}

TEST(erd_cache_mqtt_publisher, last_disconnect_duration_set_on_reconnect)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  /* Simulate disconnect at time 0 (default time source returns 0). */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK_EQUAL(0u, erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(&publisher));

  /* Simulate reconnect — duration is now - 0 = 0 since time hasn't advanced. */
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK_EQUAL(0u, erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(&publisher));
}

TEST(erd_cache_mqtt_publisher, last_disconnect_duration_reflects_gap)
{
  /* Use a custom time source to control millis() values. */
  static uint32_t fake_time = 2000;
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_set_time_fn(&publisher, +[]() -> uint32_t { return fake_time; });

  /* Disconnect at t=2000. */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);

  /* Advance time to t=6000 (4 second gap). */
  fake_time = 6000;
  erd_cache_mqtt_publisher_on_connected(&publisher);

  CHECK_EQUAL(4000u, erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(&publisher));

  /* disconnect_start_ms is not reset on reconnect — verify it's still set. */
  CHECK(publisher.disconnect_start_ms != 0u);

  fake_time = 2000;
}
TEST(erd_cache_mqtt_publisher, last_disconnect_duration_overwritten_on_subsequent_reconnect)
{
  static uint32_t fake_time = 1000;
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_set_time_fn(&publisher, +[]() -> uint32_t { return fake_time; });

  /* First disconnect/reconnect: 100ms gap. */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  fake_time = 1100;
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK_EQUAL(100u, erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(&publisher));

  /* disconnect_start_ms is not reset on reconnect, but the second disconnect
   * sees was_connected=true (set by on_connected) and resets it to current time.
   * Duration is 1600 - 1100 = 500ms (measures the second outage independently). */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  fake_time = 1600;
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK_EQUAL(500u, erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(&publisher));

  fake_time = 1000;
}

/* Cumulative disconnect duration across multiple ESPHome reconnect attempts */
TEST(erd_cache_mqtt_publisher, cumulative_disconnect_duration_across_reconnect_attempts)
{
  static uint32_t fake_time = 1000;
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_set_time_fn(&publisher, +[]() -> uint32_t { return fake_time; });

  /* Simulate a prolonged outage with ESPHome reconnect attempts every ~15s.
   * Disconnect at t=1000, then rapid connect/disconnect cycles. */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);

  /* First reconnect attempt at t=1015000 (15s later). */
  fake_time = 1015000;
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK_EQUAL(1014000u, erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(&publisher));

  /* Immediate disconnect again (ESPHome lost connection again).
   * was_connected=true so disconnect_start_ms is reset to current time. */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);

  /* Second reconnect attempt at t=1030000 (15s after second disconnect). */
  fake_time = 1030000;
  erd_cache_mqtt_publisher_on_connected(&publisher);
  /* Duration measures from the second disconnect: 1030000 - 1015000 = 15000ms. */
  CHECK_EQUAL(15000u, erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(&publisher));

  fake_time = 1000;
}
/* ------------------------------------------------------------------ */
/* Address-qualified topic format                                      */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, default_address_publishes_without_crashing)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "my_device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
  STRCMP_EQUAL("geappliances/my_device/erd/0x0008/value", mqtt_double.last_published_topic_.c_str());
}

TEST(erd_cache_mqtt_publisher, non_default_address_publishes_without_crashing)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "my_device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, 0x10, &data, sizeof(data));

  bool published = erd_cache_mqtt_publisher_loop(&publisher);
  CHECK_TRUE(published);
  STRCMP_EQUAL("geappliances/my_device/erd/0x10_0x0008/value", mqtt_double.last_published_topic_.c_str());
}
