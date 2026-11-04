/*!
 * @file
 * @brief Unit tests for the ERD cache MQTT publisher pause/resume/
 *        first_round_done API.
 */

extern "C" {
#include "erd_cache.h"
#include "erd_cache_mqtt_publisher.h"
}

#include "esphome_mqtt_client_adapter.h"
#include "double/mqtt_test_double.hpp"
#include "double/esphome_hal_double.hpp"

#include "CppUTest/TestHarness.h"

/* ------------------------------------------------------------------ */
/* Test group                                                          */
/* ------------------------------------------------------------------ */

TEST_GROUP(erd_cache_mqtt_publisher_pause)
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
    /* publish_one() reports only successful publishes; without a live
     * double every publish would silently fail and the assertions below
     * would verify nothing. */
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
/* pause() sets paused=true and resets first_round_done=false          */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, pause_sets_paused_and_resets_first_round_done)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  /* Initially not paused, first_round_done is false (memset zero). */
  CHECK_FALSE(publisher.paused);
  CHECK_FALSE(publisher.first_round_done);

  /* Simulate first_round_done being true (e.g. after a full round). */
  publisher.first_round_done = true;

  erd_cache_mqtt_publisher_pause(&publisher);

  CHECK_TRUE(publisher.paused);
  CHECK_FALSE(publisher.first_round_done);
}

/* ------------------------------------------------------------------ */
/* resume() sets paused=false                                          */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, resume_sets_paused_false)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  publisher.paused = true;

  erd_cache_mqtt_publisher_resume(&publisher);

  CHECK_FALSE(publisher.paused);
}

/* ------------------------------------------------------------------ */
/* publish_one() skips publishing when paused                           */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, publish_skips_when_paused)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  /* Pause before publishing. */
  erd_cache_mqtt_publisher_pause(&publisher);

  CHECK_TRUE(publisher.paused);
  CHECK_FALSE(publisher.first_round_done);

  /* publish_one() no-ops while paused and counts a missed loop. */
  CHECK_FALSE(erd_cache_mqtt_publisher_publish_one(&publisher));
  CHECK(publisher.missed_loops > 0);
  CHECK_EQUAL(0u, publisher.total_published);

  /* Verify that first_round_done() accessor returns false. */
  CHECK_FALSE(erd_cache_mqtt_publisher_first_round_done(&publisher));
}

/* ------------------------------------------------------------------ */
/* loop() resumes publishing after resume()                            */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, publish_resumes_publishing_after_resume)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  /* Pause, then resume. */
  erd_cache_mqtt_publisher_pause(&publisher);
  CHECK_TRUE(publisher.paused);

  erd_cache_mqtt_publisher_resume(&publisher);
  CHECK_FALSE(publisher.paused);

  /* After resume, publishing should work. */
  bool published = erd_cache_mqtt_publisher_publish_one(&publisher);
  CHECK_TRUE(published);
}

/* ------------------------------------------------------------------ */
/* first_round_done() returns false after resume, true after full pass */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, first_round_done_false_after_resume_true_after_full_pass)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* Insert a single ERD so a full round is one entry. */
  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, 0xFF, &data, sizeof(data));

  /* Pause and resume — first_round_done should be false after resume
   * (pause resets it, resume does not set it). */
  erd_cache_mqtt_publisher_pause(&publisher);
  erd_cache_mqtt_publisher_resume(&publisher);

  CHECK_FALSE(erd_cache_mqtt_publisher_first_round_done(&publisher));

  /* Publish the ERD, then publish again to scan the empty cache
   * and set first_round_done = true. */
  bool published = erd_cache_mqtt_publisher_publish_one(&publisher);
  CHECK_TRUE(published);

  /* Second call finds no pending entries, setting first_round_done. */
  published = erd_cache_mqtt_publisher_publish_one(&publisher);
  CHECK_FALSE(published);

  CHECK_TRUE(erd_cache_mqtt_publisher_first_round_done(&publisher));
}

/* ------------------------------------------------------------------ */
/* pause/resume is idempotent                                          */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, pause_is_idempotent)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  /* Pause twice — should not crash and remain paused. */
  erd_cache_mqtt_publisher_pause(&publisher);
  erd_cache_mqtt_publisher_pause(&publisher);

  CHECK_TRUE(publisher.paused);
  CHECK_FALSE(publisher.first_round_done);
}

TEST(erd_cache_mqtt_publisher_pause, resume_is_idempotent)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  publisher.paused = true;

  /* Resume twice — should not crash and remain unpaused. */
  erd_cache_mqtt_publisher_resume(&publisher);
  erd_cache_mqtt_publisher_resume(&publisher);

  CHECK_FALSE(publisher.paused);
}

TEST(erd_cache_mqtt_publisher_pause, pause_resume_pause_resume_sequence)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  /* Alternate pause/resume multiple times. */
  erd_cache_mqtt_publisher_pause(&publisher);
  CHECK_TRUE(publisher.paused);

  erd_cache_mqtt_publisher_resume(&publisher);
  CHECK_FALSE(publisher.paused);

  erd_cache_mqtt_publisher_pause(&publisher);
  CHECK_TRUE(publisher.paused);

  erd_cache_mqtt_publisher_resume(&publisher);
  CHECK_FALSE(publisher.paused);
}

/* ------------------------------------------------------------------ */
/* pause/resume with null state_mutex (non-ESP-IDF path)               */
/* On non-ESP-IDF builds, state_mutex is not defined, so the direct
 * assignment path is used.  These tests verify that path works.       */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, pause_works_with_null_state_mutex)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  /* On non-ESP-IDF, state_mutex does not exist (compiled out).
   * The pause() function takes the #else path and directly sets fields. */
  publisher.first_round_done = true;

  erd_cache_mqtt_publisher_pause(&publisher);

  CHECK_TRUE(publisher.paused);
  CHECK_FALSE(publisher.first_round_done);
}

TEST(erd_cache_mqtt_publisher_pause, resume_works_with_null_state_mutex)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  publisher.paused = true;

  erd_cache_mqtt_publisher_resume(&publisher);

  CHECK_FALSE(publisher.paused);
}

TEST(erd_cache_mqtt_publisher_pause, first_round_done_works_with_null_state_mutex)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  /* Initially false. */
  CHECK_FALSE(erd_cache_mqtt_publisher_first_round_done(&publisher));

  /* Manually set to true and verify accessor reads it. */
  publisher.first_round_done = true;
  CHECK_TRUE(erd_cache_mqtt_publisher_first_round_done(&publisher));
}

/* ------------------------------------------------------------------ */
/* pause resets first_round_done even when it was already false        */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, pause_resets_first_round_done_when_already_false)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  /* first_round_done is already false from init. */
  CHECK_FALSE(publisher.first_round_done);

  erd_cache_mqtt_publisher_pause(&publisher);

  /* Should still be false (idempotent reset). */
  CHECK_FALSE(publisher.first_round_done);
  CHECK_TRUE(publisher.paused);
}

/* ------------------------------------------------------------------ */
/* resume does not affect first_round_done                             */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher_pause, resume_does_not_set_first_round_done)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device",
    0xE0);

  /* Set first_round_done to true, then pause (resets it), then resume. */
  publisher.first_round_done = true;
  erd_cache_mqtt_publisher_pause(&publisher);
  CHECK_FALSE(publisher.first_round_done);

  erd_cache_mqtt_publisher_resume(&publisher);

  /* Resume only sets paused=false; it does not set first_round_done. */
  CHECK_FALSE(publisher.paused);
  CHECK_FALSE(publisher.first_round_done);
}
