/*! Address-aware Home Assistant discovery topic tests. */

#include "CppUTest/TestHarness.h"

#include "ha_discovery_manager.h"

TEST_GROUP(ha_discovery_manager)
{
};

TEST(ha_discovery_manager, uses_legacy_topic_without_board_address)
{
    char topic[128];
    ha_discovery_test_format_erd_topic(topic, sizeof(topic), "unit", "f414", "", "value");

    STRCMP_EQUAL("geappliances/unit/erd/0xf414/value", topic);
}

TEST(ha_discovery_manager, uses_address_qualified_topic_with_board_address)
{
    char topic[128];
    ha_discovery_test_format_erd_topic(topic, sizeof(topic), "unit", "f414", "a2", "value");

    STRCMP_EQUAL("geappliances/unit/erd/0xf414/address/0xa2/value", topic);
}
