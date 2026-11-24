/*! Address-aware Home Assistant discovery topic and availability tests. */

#include <cstring>

#include "ha_discovery_manager.h"
#include "ha_discovery_data.h"


#include "CppUTest/TestHarness.h"

/* The publish-backoff tests drive the real publish path through the ESPHome
 * MQTT test double, which pulls in <string>/<vector>/<functional> — CppUTest's
 * new macro must be gone before any STL header (same order as
 * erd_cache_mqtt_publisher_test.cpp). */
#ifdef new
#undef new
#endif

extern "C" {
#include "erd_cache.h"
}

#include "esphome_mqtt_client_adapter.h"
#include "double/mqtt_test_double.hpp"
#include "double/esphome_hal_double.hpp"

TEST_GROUP(ha_discovery_manager)
{
};

static const char* const SENSOR_LINE = "{\"i\":\"0001\",\"n\":\"Model Number\",\"d\":\"sensor\"}";
static const char* const BUTTON_LINE = "{\"i\":\"0001\",\"n\":\"Reset\",\"d\":\"button\"}";

TEST_GROUP(ha_discovery_manager_availability)
{
    ha_discovery_manager_t manager;
    char payload[HA_DISCOVERY_PAYLOAD_BUF_SIZE];

    void setup()
    {
        memset(&manager, 0, sizeof(manager));
        ha_discovery_manager_init(&manager);
        ha_discovery_manager_configure(&manager, "unit", NULL, NULL, 0, false, NULL, NULL);
        manager.sorted_erds[0] = 0x0001;
        manager.sorted_erds_count = 1;
        manager.payload_buf = payload;
    }

    void teardown()
    {
        /* payload is test-owned; keep cleanup() from freeing it. */
        manager.payload_buf = NULL;
        ha_discovery_manager_cleanup(&manager);
    }

    const char* build(const char* line)
    {
        CHECK_TRUE(ha_discovery_test_build_payload(&manager, line));
        return manager.payload_buf;
    }
};

/* Golden discovery topics: the payload must carry the exact topics the
 * publisher and write path use (shared erd_topic grammar). */
TEST(ha_discovery_manager_availability, payload_state_topic_matches_grammar_primary_board)
{
    const char* payload = build(SENSOR_LINE);

    CHECK(strstr(payload, "\"state_topic\":\"geappliances/unit/erd/0x0001/value\"") != NULL);
}

TEST(ha_discovery_manager_availability, payload_state_topic_matches_grammar_secondary_board)
{
    const char* payload = build("{\"i\":\"0001\",\"n\":\"Model Number\",\"d\":\"sensor\",\"a\":\"a2\"}");

    CHECK(strstr(payload, "\"state_topic\":\"geappliances/unit/erd/0xa2_0x0001/value\"") != NULL);
}

TEST(ha_discovery_manager_availability, payload_state_topic_canonicalizes_uppercase_board_address)
{
    /* The custom profile allows any hex case in "a"; the shared grammar
     * re-emits it lowercase so discovery topics always match the
     * publisher's lowercase value topics. */
    const char* payload = build("{\"i\":\"0001\",\"n\":\"Model Number\",\"d\":\"sensor\",\"a\":\"A2\"}");

    CHECK(strstr(payload, "\"state_topic\":\"geappliances/unit/erd/0xa2_0x0001/value\"") != NULL);
}

TEST(ha_discovery_manager_availability, payload_command_topic_matches_grammar)
{
    const char* payload = build("{\"i\":\"0001\",\"n\":\"System Mode\",\"d\":\"select\",\"ct\":\"{{ value }}\",\"a\":\"a2\"}");

    CHECK(strstr(payload, "\"command_topic\":\"geappliances/unit/erd/0xa2_0x0001/write\"") != NULL);
}

TEST(ha_discovery_manager_availability, rejects_board_address_matching_primary_sentinel)
{
    /* "ff"/"FF" is the GEA broadcast address the grammar reserves for the
     * primary sentinel; an explicit address-specific entity must not
     * silently bind to (and control) the primary board. */
    CHECK_FALSE(ha_discovery_test_build_payload(&manager, "{\"i\":\"0001\",\"n\":\"Model Number\",\"d\":\"sensor\",\"a\":\"ff\"}"));
    CHECK_FALSE(ha_discovery_test_build_payload(&manager, "{\"i\":\"0001\",\"n\":\"Model Number\",\"d\":\"sensor\",\"a\":\"FF\"}"));
    UNSIGNED_LONGS_EQUAL(2, manager.total_filtered);
}

TEST(ha_discovery_manager_availability, accepts_board_address_fe_below_sentinel)
{
    const char* payload = build("{\"i\":\"0001\",\"n\":\"Model Number\",\"d\":\"sensor\",\"a\":\"fe\"}");

    CHECK(strstr(payload, "\"state_topic\":\"geappliances/unit/erd/0xfe_0x0001/value\"") != NULL);
}

TEST(ha_discovery_manager_availability, rejects_non_hex_board_address)
{
    /* The two-hex-digit validation must reject any non-hex "a" before it
     * reaches the numeric conversion (strtoul would silently map it to
     * board 0x00). */
    CHECK_FALSE(ha_discovery_test_build_payload(&manager, "{\"i\":\"0001\",\"n\":\"Model Number\",\"d\":\"sensor\",\"a\":\"zz\"}"));
    CHECK_FALSE(ha_discovery_test_build_payload(&manager, "{\"i\":\"0001\",\"n\":\"Model Number\",\"d\":\"sensor\",\"a\":\"0\"}"));
    UNSIGNED_LONGS_EQUAL(2, manager.total_filtered);
}

TEST(ha_discovery_manager_availability, omits_availability_when_not_set)
{
    const char* payload = build(SENSOR_LINE);

    CHECK(strstr(payload, "avty_t") == NULL);
}

TEST(ha_discovery_manager_availability, adds_availability_topic_to_entity_payload)
{
    ha_discovery_manager_set_availability(&manager, "ge-range-gea/status", "online", "offline");

    const char* payload = build(SENSOR_LINE);

    CHECK(strstr(payload, "\"avty_t\":\"ge-range-gea/status\"") != NULL);
    CHECK(strstr(payload, "pl_avail") == NULL);
    CHECK(strstr(payload, "pl_not_avail") == NULL);
    CHECK_EQUAL('}', payload[strlen(payload) - 1]);
    CHECK(strstr(payload, ",}") == NULL);
}

TEST(ha_discovery_manager_availability, adds_availability_topic_to_button_payload)
{
    ha_discovery_manager_set_availability(&manager, "ge-range-gea/status", NULL, NULL);

    const char* payload = build(BUTTON_LINE);

    CHECK(strstr(payload, "\"avty_t\":\"ge-range-gea/status\"") != NULL);
}

TEST(ha_discovery_manager_availability, emits_non_default_availability_payloads)
{
    ha_discovery_manager_set_availability(&manager, "bridge/lwt", "up", "down");

    const char* payload = build(SENSOR_LINE);

    CHECK(strstr(payload, "\"avty_t\":\"bridge/lwt\"") != NULL);
    CHECK(strstr(payload, "\"pl_avail\":\"up\"") != NULL);
    CHECK(strstr(payload, "\"pl_not_avail\":\"down\"") != NULL);
}

TEST(ha_discovery_manager_availability, empty_topic_disables_availability)
{
    ha_discovery_manager_set_availability(&manager, "ge-range-gea/status", NULL, NULL);
    ha_discovery_manager_set_availability(&manager, "", NULL, NULL);

    CHECK(strstr(build(SENSOR_LINE), "avty_t") == NULL);
}

TEST(ha_discovery_manager_availability, rejects_topic_that_would_be_truncated)
{
    char topic[HA_DISCOVERY_AVAILABILITY_TOPIC_BUF_SIZE + 1];
    memset(topic, 'a', sizeof(topic) - 1);
    topic[sizeof(topic) - 1] = '\0';

    ha_discovery_manager_set_availability(&manager, topic, NULL, NULL);

    CHECK(strstr(build(SENSOR_LINE), "avty_t") == NULL);
}

TEST(ha_discovery_manager_availability, rejects_topic_that_needs_json_escaping)
{
    ha_discovery_manager_set_availability(&manager, "bad\"topic/status", NULL, NULL);

    CHECK(strstr(build(SENSOR_LINE), "avty_t") == NULL);
}

TEST_GROUP(ha_discovery_manager_data_hash)
{
    ha_discovery_manager_t manager;

    void setup()
    {
        memset(&manager, 0, sizeof(manager));
        ha_discovery_manager_init(&manager);
    }

    void teardown()
    {
        ha_discovery_manager_cleanup(&manager);
    }
};

TEST(ha_discovery_manager_data_hash, baseline_without_availability)
{
    LONGS_EQUAL(HA_DISCOVERY_DATA_HASH, ha_discovery_manager_data_hash(&manager));
}

TEST(ha_discovery_manager_data_hash, custom_profile_matches_legacy_formula)
{
    const uint32_t custom = 0x12345678u;
    ha_discovery_manager_set_custom_data(&manager, NULL, NULL, 0, 0, custom);

    const uint32_t expected = HA_DISCOVERY_DATA_HASH ^
        (custom + 0x9e3779b9u + (HA_DISCOVERY_DATA_HASH << 6) + (HA_DISCOVERY_DATA_HASH >> 2));
    LONGS_EQUAL(expected, ha_discovery_manager_data_hash(&manager));
}

TEST(ha_discovery_manager_data_hash, availability_topic_changes_hash)
{
    const uint32_t baseline = ha_discovery_manager_data_hash(&manager);

    ha_discovery_manager_set_availability(&manager, "ge-range-gea/status", NULL, NULL);
    const uint32_t with_topic = ha_discovery_manager_data_hash(&manager);

    CHECK(with_topic != baseline);
    LONGS_EQUAL(with_topic, ha_discovery_manager_data_hash(&manager));
}

TEST(ha_discovery_manager_data_hash, different_topics_produce_different_hashes)
{
    ha_discovery_manager_set_availability(&manager, "ge-range-gea/status", NULL, NULL);
    const uint32_t h1 = ha_discovery_manager_data_hash(&manager);

    ha_discovery_manager_set_availability(&manager, "my-range/status", NULL, NULL);
    const uint32_t h2 = ha_discovery_manager_data_hash(&manager);

    CHECK(h1 != h2);
}

TEST(ha_discovery_manager_data_hash, availability_payload_changes_hash)
{
    ha_discovery_manager_set_availability(&manager, "bridge/status", "online", "offline");
    const uint32_t h1 = ha_discovery_manager_data_hash(&manager);

    ha_discovery_manager_set_availability(&manager, "bridge/status", "up", "down");
    const uint32_t h2 = ha_discovery_manager_data_hash(&manager);

    CHECK(h1 != h2);
}

TEST(ha_discovery_manager_data_hash, empty_topic_clears_availability_state)
{
    ha_discovery_manager_set_availability(&manager, "ge-range-gea/status", "up", "down");
    ha_discovery_manager_set_availability(&manager, "", NULL, NULL);

    LONGS_EQUAL(HA_DISCOVERY_DATA_HASH, ha_discovery_manager_data_hash(&manager));
}

TEST(ha_discovery_manager_data_hash, custom_and_availability_combine)
{
    const uint32_t custom = 0x12345678u;
    ha_discovery_manager_set_custom_data(&manager, NULL, NULL, 0, 0, custom);
    const uint32_t custom_only = ha_discovery_manager_data_hash(&manager);

    ha_discovery_manager_set_availability(&manager, "ge-range-gea/status", NULL, NULL);
    const uint32_t both = ha_discovery_manager_data_hash(&manager);

    CHECK(both != custom_only);
    CHECK(both != HA_DISCOVERY_DATA_HASH);
}

/* The discovery run's large allocations — the three buffers plus the ~8 KB
 * tinfl decompressor state — live on the heap only between start() and
 * cleanup()/init(). Repeated refresh-button cycles must not orphan them. */
TEST_GROUP(ha_discovery_manager_run_memory)
{
    ha_discovery_manager_t manager;
    erd_cache_t cache;

    void setup()
    {
        memset(&manager, 0, sizeof(manager));
        erd_cache_init(&cache);
        ha_discovery_manager_init(&manager);
        ha_discovery_manager_configure(&manager, "run-mem", NULL, NULL, 0, false, &cache, NULL);
    }

    void teardown()
    {
        ha_discovery_manager_cleanup(&manager);
        erd_cache_destroy(&cache);
    }

    bool all_run_memory_allocated()
    {
        return manager.decomp_state != NULL && manager.decomp_buf != NULL &&
               manager.line_buf != NULL && manager.payload_buf != NULL;
    }

    bool all_run_memory_released()
    {
        return manager.decomp_state == NULL && manager.decomp_buf == NULL &&
               manager.line_buf == NULL && manager.payload_buf == NULL;
    }
};

TEST(ha_discovery_manager_run_memory, start_allocates_all_run_memory)
{
    CHECK_TRUE(all_run_memory_released());

    ha_discovery_manager_start(&manager);

    LONGS_EQUAL(ha_discovery_state_building, manager.state);
    CHECK_TRUE(all_run_memory_allocated());
}

TEST(ha_discovery_manager_run_memory, reinit_after_start_releases_the_run_memory)
{
    ha_discovery_manager_start(&manager);
    CHECK_TRUE(all_run_memory_allocated());

    ha_discovery_manager_init(&manager);

    LONGS_EQUAL(ha_discovery_state_idle, manager.state);
    CHECK_TRUE(all_run_memory_released());
}

/* The cleanup topic queue is a per-run heap allocation too (rec 10), and the
 * manager owns the struct that holds it. A re-init zeroes that struct, so it
 * has to release the queue: the leak detector is the assertion here, because
 * init() clears the pointer either way. */
TEST(ha_discovery_manager_run_memory, reinit_releases_the_cleanup_topic_queue)
{
    ha_discovery_cleanup_configure(&manager.cleanup, "run-mem", NULL, esphome::millis);
    CHECK_TRUE(ha_discovery_cleanup_start(&manager.cleanup));
    CHECK_TRUE(manager.cleanup.topic_buf != NULL);

    ha_discovery_manager_init(&manager);

    LONGS_EQUAL(ha_discovery_state_idle, manager.state);
    CHECK_TRUE(manager.cleanup.topic_buf == NULL);
}

/* Dropped-config-publish backoff. Drives the real publish path — the generated
 * discovery data is inflated with tinfl and published through the ESPHome MQTT
 * double — so the only variable is whether the backend accepts the publish.
 * Only the first few common-category ERDs are registered, so the run publishes a
 * short series of entities and every publish attempt is visible on the double. */
TEST_GROUP(ha_discovery_manager_publish_backoff)
{
    ha_discovery_manager_t manager;
    erd_cache_t cache;
    esphome_mqtt_client_adapter_t adapter;
    esphome::mqtt::MqttTestDouble mqtt_double;

    void setup()
    {
        memset(&manager, 0, sizeof(manager));
        erd_cache_init(&cache);
        /* Register the first common-category ERDs so the run publishes a whole
         * series of entities (0x0005 alone yields three); every other line of
         * the run is filtered as unregistered. */
        static const uint16_t kRegisteredErds[] = {0x0001, 0x0002, 0x0004, 0x0005,
                                                   0x0006, 0x0007, 0x0008, 0x0009};
        uint8_t value = 0x42;
        for (uint16_t erd : kRegisteredErds) {
            erd_cache_update(&cache, (tiny_erd_t)erd, 0xFF, &value, sizeof(value));
        }
        esphome::mqtt::global_mqtt_client = &mqtt_double;
        mqtt_double.connected_ = true;
        esphome_mqtt_client_adapter_init(&adapter, "backoff-test");

        ha_discovery_manager_init(&manager);
        ha_discovery_manager_set_time_fn(&manager, esphome_hal_double_get_millis);
        ha_discovery_manager_configure(&manager, "backoff-test", NULL, NULL, 0,
                                       false, &cache, &adapter.interface);
        /* Frozen clock: 1000 ms at start, so an armed window lands at 1064. */
        esphome_hal_double_set_millis(1000);
        ha_discovery_manager_start(&manager);
    }

    void teardown()
    {
        ha_discovery_manager_cleanup(&manager);
        esphome_mqtt_client_adapter_destroy(&adapter);
        esphome::mqtt::global_mqtt_client = nullptr;
        erd_cache_destroy(&cache);
        /* The HAL double's clock is a process-wide global; the roll-over test
         * leaves it near 2^32, so return it to 0 for any test that never
         * installs a fake. */
        esphome_hal_double_set_millis(0);
    }

    /* run() until the manager makes one more publish attempt. Bounded so a run
     * that stops attempting fails the test instead of hanging it. */
    bool attempts_one_more_publish(int previous_attempts)
    {
        for (int i = 0; i < 10000 &&
                      mqtt_double.publish_calls_ <= previous_attempts; i++) {
            ha_discovery_manager_run(&manager);
        }
        return mqtt_double.publish_calls_ > previous_attempts;
    }
};

TEST(ha_discovery_manager_publish_backoff, dropped_publish_arms_window_and_suppresses_retries)
{
    /* The first publish lands one pacing interval before the drop, so the
     * drop below is not held back by the pacing gate. */
    esphome_hal_double_set_millis(950);
    CHECK_TRUE(attempts_one_more_publish(0));
    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    UNSIGNED_LONGS_EQUAL(0, manager.retry_backoff_until_ms);

    /* Backend queue full: the drop arms the window, measured from the attempt
     * (t=1000, zero-length publish, +64 ms). The double records the topic even
     * for a dropped publish, so this is the entity the window was armed for. */
    esphome_hal_double_set_millis(1000);
    mqtt_double.publish_should_fail_ = true;
    CHECK_TRUE(attempts_one_more_publish(1));
    LONGS_EQUAL(2, mqtt_double.publish_calls_);
    UNSIGNED_LONGS_EQUAL(1064, manager.retry_backoff_until_ms);
    const std::string dropped_topic = mqtt_double.last_published_topic_;
    CHECK_TRUE(!dropped_topic.empty());

    /* While the window is open run() must not attempt at all — retrying on
     * every main-loop pass against a full queue is the storm this removes. The
     * entity stays at its offset, so nothing is lost by waiting. */
    mqtt_double.publish_should_fail_ = false;
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    LONGS_EQUAL(2, mqtt_double.publish_calls_);

    /* Past the window the SAME entity is retried and goes through — the topic
     * pins it: a regression that advanced the offset on the drop would retry
     * the next entity here and silently lose this one. The window is cleared
     * for the next entity. */
    esphome_hal_double_set_millis(1300);
    CHECK_TRUE(attempts_one_more_publish(2));
    LONGS_EQUAL(3, mqtt_double.publish_calls_);
    STRCMP_EQUAL(dropped_topic.c_str(), mqtt_double.last_published_topic_.c_str());
    UNSIGNED_LONGS_EQUAL(0, manager.retry_backoff_until_ms);
}

/* The armed window must survive the ~49-day millis() wrap. A plain unsigned
 * now < until compare gets this wrong in one direction or the other depending
 * on which side of the wrap it lands, and a rollover-safe diff against a
 * cleared (0) window blocks for ~25 days, so both halves are pinned here. */
TEST(ha_discovery_manager_publish_backoff, armed_window_survives_the_millis_roll_over)
{
    /* Arm the window 16 ms before the counter wraps; a cleared window must not
     * block at this clock value. The first publish lands one pacing interval
     * earlier so the drop is not held back by the pacing gate. */
    esphome_hal_double_set_millis(0xFFFFFFF0u - 50u);
    CHECK_TRUE(attempts_one_more_publish(0));

    esphome_hal_double_set_millis(0xFFFFFFF0u);
    mqtt_double.publish_should_fail_ = true;
    CHECK_TRUE(attempts_one_more_publish(1));
    /* 0xFFFFFFF0 + 64 wraps to 48. */
    UNSIGNED_LONGS_EQUAL(48, manager.retry_backoff_until_ms);

    /* 32 ms after the wrap only 32 of the 64 ms have passed: no attempt. */
    mqtt_double.publish_should_fail_ = false;
    esphome_hal_double_set_millis(0xFFFFFFF0u + 32u);
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    LONGS_EQUAL(2, mqtt_double.publish_calls_);

    /* 100 ms after the arming attempt the window has elapsed across the wrap,
     * and the entity goes through. */
    esphome_hal_double_set_millis(0xFFFFFFF0u + 100u);
    CHECK_TRUE(attempts_one_more_publish(2));
    LONGS_EQUAL(3, mqtt_double.publish_calls_);
}

/* Publish pacing (issue #209). Same fixture as the backoff tests: the real run,
 * the real publish path, and the HAL double's clock, which only moves when a
 * test moves it -- so any number of run() calls at one clock value is "as many
 * main-loop iterations as the window can hold". */

TEST(ha_discovery_manager_publish_backoff, pacing_suppresses_inside_the_window_and_passes_at_the_boundary)
{
    CHECK_TRUE(attempts_one_more_publish(0));
    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    UNSIGNED_LONGS_EQUAL(1000, manager.last_publish_ms);

    /* Inside the window run() makes no attempt at all, however often the main
     * loop calls it -- and that is not the drop backoff, which is not armed. */
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    esphome_hal_double_set_millis(1049);
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    UNSIGNED_LONGS_EQUAL(0, manager.retry_backoff_until_ms);

    /* Exactly one interval later the next config goes out and restamps: the
     * window is exclusive at its end, which pins the comparison direction. */
    esphome_hal_double_set_millis(1050);
    CHECK_TRUE(attempts_one_more_publish(1));
    LONGS_EQUAL(2, mqtt_double.publish_calls_);
    UNSIGNED_LONGS_EQUAL(1050, manager.last_publish_ms);

    /* And it paces again from the new stamp, not from the first one. */
    esphome_hal_double_set_millis(1099);
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    LONGS_EQUAL(2, mqtt_double.publish_calls_);
}

TEST(ha_discovery_manager_publish_backoff, pacing_never_holds_back_the_first_publish_of_a_run)
{
    /* A run starting in the first 50 ms of uptime: with a zero stamp the gate
     * would read uptime as the gap and hold the first config back, so 0 must
     * mean "nothing published yet". */
    UNSIGNED_LONGS_EQUAL(0, manager.last_publish_ms);
    esphome_hal_double_set_millis(10);
    CHECK_TRUE(attempts_one_more_publish(0));
    UNSIGNED_LONGS_EQUAL(10, manager.last_publish_ms);

    /* From then on the run is paced as usual. */
    esphome_hal_double_set_millis(59);
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    esphome_hal_double_set_millis(60);
    CHECK_TRUE(attempts_one_more_publish(1));
}

TEST(ha_discovery_manager_publish_backoff, pacing_window_is_rollover_safe)
{
    /* Stamp 16 ms before the counter wraps. */
    esphome_hal_double_set_millis(0xFFFFFFF0u);
    CHECK_TRUE(attempts_one_more_publish(0));
    UNSIGNED_LONGS_EQUAL(0xFFFFFFF0u, manager.last_publish_ms);

    /* 0x20 after the wrap is 48 ms on: still paced, and not restamped. */
    esphome_hal_double_set_millis(0x20u);
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    UNSIGNED_LONGS_EQUAL(0xFFFFFFF0u, manager.last_publish_ms);

    /* 0x22 is 50 ms on: the window has elapsed across the wrap. */
    esphome_hal_double_set_millis(0x22u);
    CHECK_TRUE(attempts_one_more_publish(1));
    UNSIGNED_LONGS_EQUAL(0x22u, manager.last_publish_ms);
}

TEST(ha_discovery_manager_publish_backoff, pacing_never_locks_out_after_a_gap_above_2_to_the_31)
{
    /* Only a publish moves the stamp, so a window that keeps reading as open
     * can never be left. A signed elapsed test reads any gap above 2^31 ms as
     * negative -- inside the window -- and would suppress every later attempt;
     * the unsigned one sees the true gap. Not reachable in a minutes-long run
     * today; this pins the form so it cannot become reachable. */
    CHECK_TRUE(attempts_one_more_publish(0));
    esphome_hal_double_set_millis(1000u + 0x80000000u);
    CHECK_TRUE(attempts_one_more_publish(1));
}

TEST(ha_discovery_manager_publish_backoff, dropped_publish_still_backs_off_and_does_not_restamp)
{
    CHECK_TRUE(attempts_one_more_publish(0));
    UNSIGNED_LONGS_EQUAL(1000, manager.last_publish_ms);

    /* Past the pacing window the next attempt is dropped: the backoff window
     * is armed from the attempt, and the pacing stamp stays on the last
     * publish that went out. */
    esphome_hal_double_set_millis(1050);
    mqtt_double.publish_should_fail_ = true;
    CHECK_TRUE(attempts_one_more_publish(1));
    LONGS_EQUAL(2, mqtt_double.publish_calls_);
    UNSIGNED_LONGS_EQUAL(1114, manager.retry_backoff_until_ms);
    UNSIGNED_LONGS_EQUAL(1000, manager.last_publish_ms);

    /* 100 ms after the last publish the pacing gate is open, but the drop's
     * backoff still holds every attempt: pacing does not replace it. */
    mqtt_double.publish_should_fail_ = false;
    esphome_hal_double_set_millis(1113);
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    LONGS_EQUAL(2, mqtt_double.publish_calls_);

    /* At the end of the backoff the retry goes out and restamps, and the
     * next config is paced from it. */
    esphome_hal_double_set_millis(1114);
    CHECK_TRUE(attempts_one_more_publish(2));
    UNSIGNED_LONGS_EQUAL(1114, manager.last_publish_ms);
    UNSIGNED_LONGS_EQUAL(0, manager.retry_backoff_until_ms);
    esphome_hal_double_set_millis(1163);
    for (int i = 0; i < 100; i++) {
        ha_discovery_manager_run(&manager);
    }
    LONGS_EQUAL(3, mqtt_double.publish_calls_);
    esphome_hal_double_set_millis(1164);
    CHECK_TRUE(attempts_one_more_publish(3));
}

TEST(ha_discovery_manager_publish_backoff, reinit_clears_the_pacing_stamp)
{
    /* Every run starts unpaced: init() is what a new run goes through
     * (DiscoveryLifecycleManager::begin_publish_()), and the next run's first
     * config must not wait on the previous run's last one. */
    CHECK_TRUE(attempts_one_more_publish(0));
    CHECK_TRUE(manager.last_publish_ms != 0);
    ha_discovery_manager_init(&manager);
    UNSIGNED_LONGS_EQUAL(0, manager.last_publish_ms);
}
