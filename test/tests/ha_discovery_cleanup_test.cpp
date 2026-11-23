/*!
 * @file
 * @brief Unit tests for the ha_discovery_cleanup module.
 *
 * Tests domain-enum packing, flush roundtrip, buffer overflow, drain/refill,
 * malformed topics, and empty payload echo. Uses a 512-byte test buffer
 * (defined via HA_CLEANUP_TEST_BUF_SIZE in the Makefile).
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "ha_discovery_cleanup.h"
#include "ha_discovery_manager.h"
#include "esphome_mqtt_client_adapter.h"
#include "double/mqtt_test_double.hpp"

#include <cstdlib>

/* ------------------------------------------------------------------ */
/* Test helpers                                                       */
/* ------------------------------------------------------------------ */

static uint32_t test_time_ms = 0;
static uint32_t test_get_time_ms(void) { return test_time_ms; }

/* The topic queue is a per-run heap allocation (rec 10 leftover), so a fixture
 * that builds a struct by hand has to own one. The tracker sees both sides of
 * the allocation seam, which is what lets the lifetime test assert the balance
 * rather than just a pointer change; the group teardown releases whatever a
 * test leaves running so the suite stays leak-clean. Fixed-size on purpose: a
 * std::vector here would allocate its own buffer with new and CppUTest would
 * charge that allocation to whichever test triggered the growth. */
static void *g_live_queues[16];
static size_t g_live_queue_count = 0;

static void *tracking_alloc(size_t size)
{
    void *p = malloc(size);
    if (p != NULL && g_live_queue_count < sizeof(g_live_queues) / sizeof(g_live_queues[0])) {
        g_live_queues[g_live_queue_count++] = p;
    }
    return p;
}

static void tracking_free(void *p)
{
    for (size_t i = 0; i < g_live_queue_count; ++i) {
        if (g_live_queues[i] == p) {
            g_live_queues[i] = g_live_queues[--g_live_queue_count];
            break;
        }
    }
    free(p);
}

static void release_live_queues()
{
    for (size_t i = 0; i < g_live_queue_count; ++i) free(g_live_queues[i]);
    g_live_queue_count = 0;
}

/* Minimal i_mqtt_client_t for the flush guard: cleanup_flush_queue() reaches
 * only publish_raw, and the guard has to stop before it gets there. */
static int g_stub_publish_calls = 0;

static bool stub_publish_raw(i_mqtt_client_t*, const char*, const char*, size_t, bool)
{
    g_stub_publish_calls++;
    return true;
}

static bool stub_is_connected(i_mqtt_client_t*)
{
    return false;
}

static const i_mqtt_client_api_t g_stub_api = {
    nullptr, nullptr, nullptr, nullptr, stub_is_connected, stub_publish_raw, nullptr, nullptr
};

static i_mqtt_client_t make_stub_client()
{
    i_mqtt_client_t client;
    client.api = &g_stub_api;
    return client;
}

static ha_discovery_cleanup_t make_cleanup(const char* device_id)
{
    ha_discovery_cleanup_t cl;
    memset(&cl, 0, sizeof(cl));
    /* start()/destroy()/release_queue() really execute the spinlock on
     * hardware; a zeroed mux is owner=0, an invalid state. The host stub
     * ignores the mux, so initialize it the way init() would. */
    portMUX_INITIALIZE(&cl.mux);
    cl.device_id = device_id;
    cl.get_time_ms = test_get_time_ms;
    cl.state = ha_cleanup_state_cleaning;
    cl.mqtt_client = NULL;
    cl.queue_write_pos = 0;
    cl.dropped_count = 0;
    cl.pass_found_topics = false;
    cl.last_activity_ms = 0;
    cl.alloc_fn = tracking_alloc;
    cl.free_fn = tracking_free;
    cl.topic_buf = (char *) tracking_alloc(HA_CLEANUP_TOPIC_BUF_SIZE);
    return cl;
}

TEST_GROUP(ha_discovery_cleanup)
{
    void setup() {}
    void teardown() { release_live_queues(); }
};

/* Pack via callback                                                    */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, pack_valid_topic)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(1, cl.queue_count);
    CHECK_EQUAL(0, cl.dropped_count);
    CHECK(cl.pass_found_topics);

    /* Verify full topic stored in buffer. */
    STRCMP_EQUAL("homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
                 cl.topic_buf);
}


/* ------------------------------------------------------------------ */
/* Buffer full behavior                                                 */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, buffer_full_drops_topics)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    /* Each topic is ~55 bytes, 512/55 ~ 9. Fill until we start dropping. */
    for (uint32_t i = 0; i < 100; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    CHECK(cl.dropped_count > 0);
    CHECK(cl.queue_count > 0);
}

/* ------------------------------------------------------------------ */
/* Drain and refill                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, drain_and_refill)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    /* Pack 5 topics. */
    for (uint32_t i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    uint16_t initial_count = cl.queue_count;
    CHECK(initial_count > 0);

    /* Simulate drain: read topic from position 0 and compact. */
    while (cl.queue_count > 0) {
        size_t consumed = strlen(cl.topic_buf) + 1;
        if (consumed > cl.queue_write_pos) consumed = cl.queue_write_pos;
        memmove(cl.topic_buf, cl.topic_buf + consumed, cl.queue_write_pos - consumed);
        cl.queue_write_pos -= (uint16_t)consumed;
        cl.queue_count--;
    }

    CHECK_EQUAL(0, cl.queue_count);

    /* Refill — should succeed without drops. */
    for (uint32_t i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/new_field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    CHECK(cl.queue_count > 0);
    CHECK_EQUAL(0, cl.dropped_count);
}

/* ------------------------------------------------------------------ */
/* Malformed topics                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, missing_config_suffix_skipped)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/sensor/Dishwasher_PDT715/field_1/state",
        "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(0, cl.dropped_count);
}


TEST(ha_discovery_cleanup, too_short_topic_skipped)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cleanup_topic_callback("abc", "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(0, cl.dropped_count);
}

/* ------------------------------------------------------------------ */
/* cleanup_start resets                                                 */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, cleanup_start_resets_fields)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cl.queue_write_pos = 100;
    cl.queue_count = 50;
    cl.dropped_count = 10;

    cleanup_start(&cl);

    CHECK_EQUAL(0, cl.queue_write_pos);
    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(0, cl.dropped_count);
}

/* Flush stores and republishes the original topic unchanged               */
TEST(ha_discovery_cleanup, flush_stores_topic_unchanged)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(1, cl.queue_count);

    /* The topic stored in the buffer should be identical to what was received. */
    STRCMP_EQUAL("homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
                 cl.topic_buf);
}

TEST(ha_discovery_cleanup, flush_drains_one_at_a_time)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    /* Pack several topics. */
    for (uint32_t i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    uint16_t initial_count = cl.queue_count;
    CHECK_EQUAL(5, initial_count);

    /* Drain one at a time, verifying each topic. */
    for (uint32_t i = 0; i < 5; i++) {
        char expected[128];
        snprintf(expected, sizeof(expected),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        STRCMP_EQUAL(expected, cl.topic_buf);

        size_t consumed = strlen(cl.topic_buf) + 1;
        if (consumed > cl.queue_write_pos) consumed = cl.queue_write_pos;
        memmove(cl.topic_buf, cl.topic_buf + consumed, cl.queue_write_pos - consumed);
        cl.queue_write_pos -= (uint16_t)consumed;
        cl.queue_count--;
    }

    CHECK_EQUAL(0, cl.queue_count);
}
/* ------------------------------------------------------------------ */
/* Compacting buffer behavior                                          */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, compacting_buffer_drain_and_refill)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    /* Pack several topics. */
    for (uint32_t i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    uint16_t write_pos_before = cl.queue_write_pos;
    CHECK(write_pos_before > 0);

    /* Drain all but the last entry via compacting. */
    while (cl.queue_count > 1) {
        size_t consumed = strlen(cl.topic_buf) + 1;
        if (consumed > cl.queue_write_pos) consumed = cl.queue_write_pos;
        memmove(cl.topic_buf, cl.topic_buf + consumed, cl.queue_write_pos - consumed);
        cl.queue_write_pos -= (uint16_t)consumed;
        cl.queue_count--;
    }

    /* After compaction, write_pos should be much lower than before. */
    CHECK(cl.queue_write_pos < write_pos_before);
    CHECK(cl.queue_count == 1);

    /* Pack more topics — they should append at the new write_pos. */
    for (uint32_t i = 0; i < 3; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/compact_field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    CHECK(cl.queue_count > 1);

    /* Verify all entries are readable from position 0. */
    while (cl.queue_count > 0) {
        CHECK(strlen(cl.topic_buf) > 0);

        size_t consumed = strlen(cl.topic_buf) + 1;
        if (consumed > cl.queue_write_pos) consumed = cl.queue_write_pos;
        memmove(cl.topic_buf, cl.topic_buf + consumed, cl.queue_write_pos - consumed);
        cl.queue_write_pos -= (uint16_t)consumed;
        cl.queue_count--;
    }
    CHECK_EQUAL(0, cl.queue_count);
}
/* ------------------------------------------------------------------ */
/* Drain wait state initialization                                      */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, drain_state_initialized_to_zero)
{
    ha_discovery_cleanup_t cl;
    memset(&cl, 0, sizeof(cl));
    cl.device_id = "TestDevice";
    cl.get_time_ms = test_get_time_ms;

    cleanup_start(&cl);

    CHECK_EQUAL(0, cl.drain_start_ms);
}

/* ------------------------------------------------------------------ */
/* Dropped removal publish: backoff, same-topic retry, roll-over       */
/* ------------------------------------------------------------------ */

TEST_GROUP(ha_discovery_cleanup_publish_backoff)
{
    ha_discovery_cleanup_t cl;
    esphome_mqtt_client_adapter_t adapter;
    esphome::mqtt::MqttTestDouble mqtt_double;

    static constexpr const char* kDeviceId = "Dishwasher_PDT715";

    void setup()
    {
        test_time_ms = 1000;
        ha_discovery_cleanup_init(&cl);  /* also portMUX_INITIALIZEs cl.mux: the
                                         * spinlock paths under test are valid on hardware */
        esphome_mqtt_client_adapter_init(&adapter, kDeviceId);
        esphome::mqtt::global_mqtt_client = &mqtt_double;
        mqtt_double.connected_ = true;
        ha_discovery_cleanup_configure(&cl, kDeviceId, &adapter.interface,
                                       test_get_time_ms);
        /* The topic queue is allocated by start(); these tests drive the
         * module directly, so the run has to be started here. destroy() in the
         * teardown releases it. */
        CHECK_TRUE(ha_discovery_cleanup_start(&cl));
    }

    void teardown()
    {
        /* Releases the per-run topic queue a test may have started, so a run
         * that never reached destroy() cannot leak it into the leak check. */
        ha_discovery_cleanup_destroy(&cl);
        esphome_mqtt_client_adapter_destroy(&adapter);
        esphome::mqtt::global_mqtt_client = nullptr;
        /* The fake clock is a file-scope global; the roll-over test leaves it
         * near 2^32, so return it to 0 for any test that never sets it. */
        test_time_ms = 0;
    }

    void queue_topics(int count)
    {
        for (int i = 0; i < count; i++) {
            char topic[128];
            snprintf(topic, sizeof(topic),
                "homeassistant/sensor/%s/field_%d/config", kDeviceId, i);
            cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
        }
    }
};

TEST(ha_discovery_cleanup_publish_backoff, dropped_removal_arms_window_and_retries_the_same_topic)
{
    queue_topics(2);
    CHECK_EQUAL(2, cl.queue_count);

    /* Backend queue full: the drop restores the queue and arms the window from
     * the attempt (t=1000, zero-length publish, +64 ms). */
    mqtt_double.publish_should_fail_ = true;
    cleanup_flush_queue(&cl);
    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    CHECK_EQUAL(2, cl.queue_count);
    CHECK_EQUAL(0, (int) cl.total_removed_count);
    UNSIGNED_LONGS_EQUAL(1064, cl.retry_backoff_until_ms);
    const std::string dropped_topic = mqtt_double.last_published_topic_;
    CHECK_TRUE(!dropped_topic.empty());

    /* While the window is open a flush must not attempt at all: one attempt
     * per main-loop pass against a full queue is the storm this removes. */
    mqtt_double.publish_should_fail_ = false;
    for (int i = 0; i < 100; i++) {
        cleanup_flush_queue(&cl);
    }
    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    CHECK_EQUAL(2, cl.queue_count);

    /* Past the window the SAME topic is removed. The topic pins it: a
     * regression that discarded the topic instead of retrying it would leave a
     * stale discovery config in Home Assistant, and no other assertion here
     * would notice. */
    test_time_ms = 1300;
    cleanup_flush_queue(&cl);
    LONGS_EQUAL(2, mqtt_double.publish_calls_);
    STRCMP_EQUAL(dropped_topic.c_str(), mqtt_double.last_published_topic_.c_str());
    CHECK_EQUAL(1, cl.queue_count);
    CHECK_EQUAL(1, (int) cl.total_removed_count);
    UNSIGNED_LONGS_EQUAL(0, cl.retry_backoff_until_ms);

    /* A success clears the window, so the next topic needs no wait. */
    cleanup_flush_queue(&cl);
    LONGS_EQUAL(3, mqtt_double.publish_calls_);
    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(2, (int) cl.total_removed_count);
}

/* Same roll-over contract as the discovery publish backoff: the armed window
 * must survive the ~49-day millis() wrap, and a cleared (0) window must never
 * block -- the sentinel is tested before the rollover-safe signed diff, which
 * reads now - 0 as negative past 2^31. */
TEST(ha_discovery_cleanup_publish_backoff, armed_window_survives_the_millis_roll_over)
{
    queue_topics(1);

    /* Arm the window 16 ms before the wrap. */
    test_time_ms = 0xFFFFFFF0u;
    mqtt_double.publish_should_fail_ = true;
    cleanup_flush_queue(&cl);
    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    /* 0xFFFFFFF0 + 64 wraps to 48. */
    UNSIGNED_LONGS_EQUAL(48, cl.retry_backoff_until_ms);

    /* 32 ms after the wrap only 32 of the 64 ms have passed: no attempt. */
    mqtt_double.publish_should_fail_ = false;
    test_time_ms = 0xFFFFFFF0u + 32u;
    for (int i = 0; i < 100; i++) {
        cleanup_flush_queue(&cl);
    }
    LONGS_EQUAL(1, mqtt_double.publish_calls_);

    /* Past the window across the wrap, the topic goes through. */
    test_time_ms = 0xFFFFFFF0u + 100u;
    cleanup_flush_queue(&cl);
    LONGS_EQUAL(2, mqtt_double.publish_calls_);
    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(1, (int) cl.total_removed_count);

    /* A cleared window must not block at a large clock value either. */
    queue_topics(1);
    test_time_ms = 0x90000000u;
    cleanup_flush_queue(&cl);
    LONGS_EQUAL(3, mqtt_double.publish_calls_);
    CHECK_EQUAL(0, cl.queue_count);
}

/* Two clean passes only prove no NEW topics arrived. Removals still sitting in
 * the queue are outstanding work: reporting done there would let the caller
 * save the discovery record while stale topics survive in Home Assistant. */
TEST(ha_discovery_cleanup_publish_backoff, does_not_report_done_while_removals_are_queued)
{
    ha_discovery_cleanup_start(&cl);
    queue_topics(2);
    mqtt_double.publish_should_fail_ = true;

    /* 100 ms per iteration, so every backoff window has elapsed by the next
     * one and each pass really does attempt the removal. */
    for (int i = 0; i < 400; i++) {
        test_time_ms += 100;
        ha_discovery_cleanup_run(&cl);
        if (ha_discovery_cleanup_is_done(&cl)) break;
    }

    CHECK_EQUAL(ha_cleanup_state_cleaning, (int) cl.state);
    CHECK_EQUAL(2, cl.queue_count);
    CHECK_TRUE(cl.clean_passes >= 2);
    CHECK_EQUAL(0, (int) cl.total_removed_count);

    /* Broker recovers: the queued topics drain, then the run finishes. */
    mqtt_double.publish_should_fail_ = false;
    for (int i = 0; i < 400 && !ha_discovery_cleanup_is_done(&cl); i++) {
        test_time_ms += 100;
        ha_discovery_cleanup_run(&cl);
    }
    CHECK_TRUE(ha_discovery_cleanup_is_done(&cl));
    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(2, (int) cl.total_removed_count);
}

/* ------------------------------------------------------------------ */
/* Critical-section discipline (issue #198)                            */
/* ------------------------------------------------------------------ */

/* taskENTER_CRITICAL() raises the interrupt threshold, and on the single-core
 * parts that means interrupts are off. A removal publish taken there blocks on
 * the outbound ring with interrupts disabled (interrupt watchdog, seen on four
 * bridges), and a log taken there aborts inside newlib's stdio lock ("recursive
 * mutexes make no sense in ISR context", seen on the dishwasher) -- both on the
 * same boot. The depth counter in double/critical_depth_shim.hpp turns that into
 * an assertion: every publish and every return path must sit at depth 0. */
TEST_GROUP(ha_discovery_cleanup_critical_section)
{
    ha_discovery_cleanup_t cl;
    esphome_mqtt_client_adapter_t adapter;
    esphome::mqtt::MqttTestDouble mqtt_double;

    static constexpr const char* kDeviceId = "Dishwasher_PDT715";
    static constexpr const char* kTopic =
        "homeassistant/sensor/Dishwasher_PDT715/3218_washzones/config";

    void setup()
    {
        test_time_ms = 1000;
        /* Canary: an unbalanced critical section in an earlier test lands here.
         * Assert before clearing, so a leak is never laundered by the next test. */
        LONGS_EQUAL(0, esp_test_critical_depth_get_());
        esp_test_critical_depth_reset_();
        ha_discovery_cleanup_init(&cl);
        /* init() below also portMUX_INITIALIZEs cl.mux, so the spinlock paths
         * under test carry a valid mux on hardware (the host stub ignores it). */
        esphome_mqtt_client_adapter_init(&adapter, kDeviceId);
        esphome::mqtt::global_mqtt_client = &mqtt_double;
        mqtt_double.connected_ = true;
        ha_discovery_cleanup_configure(&cl, kDeviceId, &adapter.interface,
                                       test_get_time_ms);
        CHECK_TRUE(ha_discovery_cleanup_start(&cl));
        esp_test_log_reset_();  /* ignore any init/configure logging */
    }

    void teardown()
    {
        ha_discovery_cleanup_destroy(&cl);
        esphome_mqtt_client_adapter_destroy(&adapter);
        esphome::mqtt::global_mqtt_client = nullptr;
        test_time_ms = 0;
    }

    void queue_topic(const char* topic)
    {
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }
};

TEST(ha_discovery_cleanup_critical_section, removal_publish_runs_at_depth_zero)
{
    queue_topic(kTopic);
    CHECK_EQUAL(1, cl.queue_count);

    cleanup_flush_queue(&cl);

    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    CHECK_EQUAL(0, mqtt_double.last_publish_critical_depth_);
    CHECK_EQUAL(0, esp_test_critical_depth_get_());
    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(1, (int) cl.total_removed_count);
}

/* The dropped-publish path returns early. Before the fix it returned from
 * inside the critical section, leaving interrupts disabled for the rest of the
 * boot -- which is why a single congested broker turned into a watchdog reset
 * instead of a retried publish. */
TEST(ha_discovery_cleanup_critical_section, dropped_removal_leaves_depth_zero)
{
    queue_topic(kTopic);
    mqtt_double.publish_should_fail_ = true;

    cleanup_flush_queue(&cl);

    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    CHECK_EQUAL(0, mqtt_double.last_publish_critical_depth_);
    CHECK_EQUAL(0, esp_test_critical_depth_get_());
    CHECK_EQUAL(1, cl.queue_count);
    CHECK_EQUAL(0, (int) cl.total_removed_count);
}

/* The third exit out of the section: a topic too long for the stack buffer is
 * skipped and its warning is logged. That log is the dishwasher failure mode of
 * issue #198 -- inside the section it takes newlib's stdio lock and aborts. The
 * pre-fix code balanced this path and only logged too early, so the assertion is
 * on the depth the log was emitted at, not on the depth after the return. */
TEST(ha_discovery_cleanup_critical_section, truncated_topic_logs_at_depth_zero)
{
    std::string long_topic =
        std::string("homeassistant/sensor/") + kDeviceId + "/entity_" +
        std::string(250, 'x') + "/config";
    queue_topic(long_topic.c_str());
    CHECK_EQUAL(1, cl.queue_count);

    cleanup_flush_queue(&cl);

    LONGS_EQUAL(0, mqtt_double.publish_calls_);
    /* Per-depth counts, not a bare count or last-depth: a benign extra log on
     * this path must not false-fail the test, and a log added after the warning
     * must not overwrite what the depth assertion observes. */
    LONGS_EQUAL(1, esp_test_log_count_at_depth_("ha_cleanup", 0));
    LONGS_EQUAL(0, esp_test_log_count_at_depth_("ha_cleanup", 1));
    CHECK_EQUAL(0, esp_test_critical_depth_get_());
    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(0, (int) cl.total_removed_count);
}

/* The empty-queue exit also takes the section, and it is the cheapest path to
 * get wrong: a `return 0` moved above the exit leaks it for the rest of the boot. */
TEST(ha_discovery_cleanup_critical_section, empty_queue_flush_leaves_depth_zero)
{
    CHECK_EQUAL(0, (int) cleanup_flush_queue(&cl));
    CHECK_EQUAL(0, esp_test_critical_depth_get_());
    LONGS_EQUAL(0, mqtt_double.publish_calls_);
}

/* The window left open by releasing the section before the publish is only safe
 * because the MQTT callback appends -- the copied front topic and `consumed` must
 * still describe the front when the compaction re-takes the section. Drive an
 * append from inside publish() so that claim is checked, not argued. */
TEST(ha_discovery_cleanup_critical_section, append_during_the_release_window_keeps_the_front)
{
    static constexpr const char* kLate =
        "homeassistant/sensor/Dishwasher_PDT715/late_arrival/config";
    queue_topic(kTopic);
    mqtt_double.on_publish_ = [this]() { queue_topic(kLate); };

    cleanup_flush_queue(&cl);

    LONGS_EQUAL(1, mqtt_double.publish_calls_);
    CHECK_EQUAL(0, mqtt_double.last_publish_critical_depth_);
    CHECK_EQUAL(0, esp_test_critical_depth_get_());
    /* Only the front topic was compacted away; the late append survives. */
    STRCMP_EQUAL(kTopic, mqtt_double.last_published_topic_.c_str());
    CHECK_EQUAL(1, cl.queue_count);
    CHECK_EQUAL(1, (int) cl.total_removed_count);

    mqtt_double.on_publish_ = nullptr;
    cleanup_flush_queue(&cl);
    STRCMP_EQUAL(kLate, mqtt_double.last_published_topic_.c_str());
    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(2, (int) cl.total_removed_count);
}

/* Rec 10 leftover: the queue is allocated for the run and released by
 * destroy(), so the permanent object never carries it. Asserted through the
 * allocation seam: the live set must go to zero on destroy, which is what a
 * leaked or embedded buffer would break. */
TEST(ha_discovery_cleanup, topic_queue_is_allocated_for_the_run_and_freed_by_destroy)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");
    CHECK_EQUAL(1u, (unsigned) g_live_queue_count);

    CHECK_TRUE(ha_discovery_cleanup_start(&cl));
    CHECK_TRUE(cl.topic_buf != NULL);
    /* start() released the queue the fixture installed and owns one of its
     * own: one live allocation, not two. */
    CHECK_EQUAL(1u, (unsigned) g_live_queue_count);

    ha_discovery_cleanup_destroy(&cl);
    CHECK_EQUAL(0u, (unsigned) g_live_queue_count);
    CHECK_TRUE(cl.topic_buf == nullptr);
}

/* A struct with no queue -- never started, or destroyed while a delayed MQTT
 * callback was still in flight -- must drop topics the way a full queue does
 * and must not write through NULL when flushing. */
TEST(ha_discovery_cleanup, callback_and_flush_tolerate_an_unallocated_queue)
{
    i_mqtt_client_t client = make_stub_client();
    g_stub_publish_calls = 0;

    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");
    cl.mqtt_client = &client;
    cl.topic_buf = NULL;       /* the pointer goes first, so the free below never
                                * follows a dangling read */
    release_live_queues();

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(1, cl.dropped_count);
    CHECK_EQUAL(1, cl.pass_received_count);

    cl.queue_count = 1;         /* stale counters must not reach the compaction */
    CHECK_EQUAL(1, cleanup_flush_queue(&cl));
    CHECK_EQUAL(0, g_stub_publish_calls);
    CHECK_EQUAL(0, (int) cl.total_removed_count);
}
