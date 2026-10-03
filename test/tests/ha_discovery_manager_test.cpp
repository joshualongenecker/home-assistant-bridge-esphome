/*! Address-aware Home Assistant discovery topic and availability tests. */

#include <cstring>

#include "ha_discovery_manager.h"
#include "ha_discovery_data.h"


#include "CppUTest/TestHarness.h"

TEST_GROUP(ha_discovery_manager)
{
};

TEST(ha_discovery_manager, uses_legacy_topic_without_board_address)
{
    char topic[128];
    ha_discovery_test_format_erd_topic(topic, sizeof(topic), "unit", "f414", "", "value");

    STRCMP_EQUAL("geappliances/unit/erd/0xf414/value", topic);
}

TEST(ha_discovery_manager, combines_board_address_and_erd_in_non_primary_topic)
{
    char topic[128];
    ha_discovery_test_format_erd_topic(topic, sizeof(topic), "unit", "f414", "a2", "value");

    STRCMP_EQUAL("geappliances/unit/erd/0xa2_0xf414/value", topic);
}

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
