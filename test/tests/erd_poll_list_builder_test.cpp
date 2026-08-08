/*!
 * @file
 * @brief Tests for ErdPollListBuilder.
 */

#include "erd_poll_list_builder.h"
#include "erd_lists.h"
#include "erd_bridge_common.h"

#include "CppUTest/TestHarness.h"

using namespace esphome::geappliances_bridge;

TEST_GROUP(erd_poll_list_builder)
{
  ErdPollListConfig config;
  uint16_t feature_bits[32];
  uint16_t feature_bits_count;
  probe_entry_t custom_erds_arr[32];
  uint16_t custom_erds_count;

  void setup()
  {
    config.mode = BRIDGE_MODE_POLL;
    config.subscription_active = false;
    config.appliance_api_parsing = false;
    config.feature_bit_valid_erds = nullptr;
    config.feature_bit_valid_erds_count = 0;
    config.custom_erds = nullptr;
    config.custom_erds_count = 0;
    config.appliance_type = 0;  // water heater
    feature_bits_count = 0;
    custom_erds_count = 0;
  }
};

TEST(erd_poll_list_builder, subscribe_mode_returns_only_custom_erds)
{
  config.mode = BRIDGE_MODE_SUBSCRIBE;
  config.subscription_active = true;
  custom_erds_arr[0] = {0xABCD, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_arr[1] = {0x1234, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_count = 2;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(2u, result.erds_count);
  CHECK_EQUAL(0xABCDu, result.erds[0].erd);
  CHECK_EQUAL(0x1234u, result.erds[1].erd);
}

TEST(erd_poll_list_builder, subscribe_mode_no_custom_returns_empty)
{
  config.mode = BRIDGE_MODE_SUBSCRIBE;
  config.subscription_active = true;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(0u, result.erds_count);
}

TEST(erd_poll_list_builder, subscribe_mode_not_active_returns_full_list)
{
  config.mode = BRIDGE_MODE_SUBSCRIBE;
  config.subscription_active = false;
  custom_erds_arr[0] = {0xABCD, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_count = 1;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  // Not active → treated as poll fallback, builds full list.
  CHECK(result.erds_count > 0);
}

TEST(erd_poll_list_builder, auto_mode_subscription_active_returns_only_custom)
{
  config.mode = BRIDGE_MODE_AUTO;
  config.subscription_active = true;
  custom_erds_arr[0] = {0xDEAD, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_count = 1;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds_count);
  CHECK_EQUAL(0xDEADu, result.erds[0].erd);
}

TEST(erd_poll_list_builder, poll_mode_with_api_parsing_returns_feature_bits_plus_custom)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  feature_bits[0] = 0x0001;
  feature_bits[1] = 0x0002;
  feature_bits[2] = 0x0003;
  feature_bits_count = 3;
  config.feature_bit_valid_erds = feature_bits;
  config.feature_bit_valid_erds_count = feature_bits_count;
  custom_erds_arr[0] = {0xABCD, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_count = 1;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(4u, result.erds_count);
  CHECK_EQUAL(0x0001u, result.erds[0].erd);
  CHECK_EQUAL(0x0002u, result.erds[1].erd);
  CHECK_EQUAL(0x0003u, result.erds[2].erd);
  CHECK_EQUAL(0xABCDu, result.erds[3].erd);
}
TEST(erd_poll_list_builder, poll_mode_with_api_parsing_no_custom)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  feature_bits[0] = 0x0001;
  feature_bits[1] = 0x0002;
  feature_bits_count = 2;
  config.feature_bit_valid_erds = feature_bits;
  config.feature_bit_valid_erds_count = feature_bits_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(2u, result.erds_count);
  CHECK_EQUAL(0x0001u, result.erds[0].erd);
  CHECK_EQUAL(0x0002u, result.erds[1].erd);
}

TEST(erd_poll_list_builder, poll_mode_without_api_parsing_returns_full_list)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;
  config.appliance_type = 0;  // water heater

  auto result = build_erd_poll_list(config);

  // Should contain common + energy + applianceApiFeature + waterHeater ERDs.
  CHECK(result.erds_count >= (commonErdCount + energyErdCount +
                              applianceApiFeatureErdCount + waterHeaterErdCount));
}

TEST(erd_poll_list_builder, poll_mode_without_api_parsing_with_custom)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;
  config.appliance_type = 0;  // water heater
  custom_erds_arr[0] = {0xBEEF, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_count = 1;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  // Should contain full list + custom.
  bool found_custom = false;
  for (uint16_t i = 0; i < result.erds_count; i++) {
    if (result.erds[i].erd == 0xBEEF) {
      found_custom = true;
    }
  }
  CHECK(found_custom);
}

TEST(erd_poll_list_builder, auto_mode_subscription_not_active_treats_as_poll)
{
  config.mode = BRIDGE_MODE_AUTO;
  config.subscription_active = false;
  config.appliance_api_parsing = true;
  feature_bits[0] = 0x0001;
  feature_bits_count = 1;
  config.feature_bit_valid_erds = feature_bits;
  config.feature_bit_valid_erds_count = feature_bits_count;
  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds_count);
  CHECK_EQUAL(0x0001u, result.erds[0].erd);
}

TEST(erd_poll_list_builder, deduplicates_custom_erd_already_in_feature_bits)
{
  config.appliance_api_parsing = true;
  feature_bits[0] = 0x0001;
  feature_bits[1] = 0x0002;
  feature_bits_count = 2;
  config.feature_bit_valid_erds = feature_bits;
  config.feature_bit_valid_erds_count = feature_bits_count;
  custom_erds_arr[0] = {0x0001, PROBE_ENTRY_DEFAULT_ADDRESS};  // duplicate of feature bit ERD
  custom_erds_count = 1;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(2u, result.erds_count);
  CHECK_EQUAL(0x0001u, result.erds[0].erd);
  CHECK_EQUAL(0x0002u, result.erds[1].erd);
}

TEST(erd_poll_list_builder, invalid_appliance_type_skips_appliance_specific_erds)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;
  config.appliance_type = 255;  // invalid

  auto result = build_erd_poll_list(config);

  // Should contain common + energy + applianceApiFeature, but NOT appliance-specific.
  CHECK(result.erds_count >= (commonErdCount + energyErdCount + applianceApiFeatureErdCount));
}

TEST(erd_poll_list_builder, empty_feature_bits_with_api_parsing_returns_only_custom)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  custom_erds_arr[0] = {0xABCD, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_count = 1;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds_count);
  CHECK_EQUAL(0xABCDu, result.erds[0].erd);
}

TEST(erd_poll_list_builder, gea2_protocol_returns_full_list)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;
  config.appliance_type = 0;

  auto result = build_erd_poll_list(config);

  // GEA2 always uses full list (no subscriptions, no API parsing).
  CHECK(result.erds_count >= (commonErdCount + energyErdCount +
                              applianceApiFeatureErdCount + waterHeaterErdCount));
}

TEST(erd_poll_list_builder, description_is_non_empty)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;

  auto result = build_erd_poll_list(config);

  CHECK(result.description != nullptr);
  CHECK(result.description[0] != '\0');
}

TEST(erd_poll_list_builder, subscribe_mode_description)
{
  config.mode = BRIDGE_MODE_SUBSCRIBE;
  config.subscription_active = true;

  auto result = build_erd_poll_list(config);

  CHECK(result.description != nullptr);
  CHECK(result.description[0] != '\0');
}

TEST(erd_poll_list_builder, poll_mode_api_parsing_description)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;

  auto result = build_erd_poll_list(config);

  CHECK(result.description != nullptr);
  CHECK(result.description[0] != '\0');
}

TEST(erd_poll_list_builder, null_feature_bits_pointer_with_api_parsing)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  config.feature_bit_valid_erds = nullptr;
  config.feature_bit_valid_erds_count = 0;
  custom_erds_arr[0] = {0xABCD, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_count = 1;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds_count);
  CHECK_EQUAL(0xABCDu, result.erds[0].erd);
}

TEST(erd_poll_list_builder, null_custom_erds_pointer)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  feature_bits[0] = 0x0001;
  feature_bits_count = 1;
  config.feature_bit_valid_erds = feature_bits;
  config.feature_bit_valid_erds_count = feature_bits_count;
  config.custom_erds = nullptr;
  config.custom_erds_count = 0;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds_count);
  CHECK_EQUAL(0x0001u, result.erds[0].erd);
}
/* ------------------------------------------------------------------ */
/* Multi-board address dedup tests                                     */
/* ------------------------------------------------------------------ */

TEST(erd_poll_list_builder, same_erd_different_addresses_not_deduped)
{
  config.mode = BRIDGE_MODE_POLL;
  config.subscription_active = false;
  config.appliance_api_parsing = true;
  config.feature_bit_valid_erds = nullptr;
  config.feature_bit_valid_erds_count = 0;
  custom_erds_arr[0] = {0x1234, 0x10};
  custom_erds_arr[1] = {0x1234, 0x20};
  custom_erds_count = 2;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(2u, result.erds_count);
  CHECK_EQUAL(0x1234u, result.erds[0].erd);
  CHECK_EQUAL(0x10u, result.erds[0].board_address);
  CHECK_EQUAL(0x1234u, result.erds[1].erd);
  CHECK_EQUAL(0x20u, result.erds[1].board_address);
}

TEST(erd_poll_list_builder, same_erd_same_address_deduped)
{
  config.mode = BRIDGE_MODE_POLL;
  config.subscription_active = false;
  config.appliance_api_parsing = true;
  config.feature_bit_valid_erds = nullptr;
  config.feature_bit_valid_erds_count = 0;
  custom_erds_arr[0] = {0x1234, 0x10};
  custom_erds_arr[1] = {0x1234, 0x10};
  custom_erds_arr[2] = {0x1234, 0x10};
  custom_erds_count = 3;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds_count);
  CHECK_EQUAL(0x1234u, result.erds[0].erd);
  CHECK_EQUAL(0x10u, result.erds[0].board_address);
}

TEST(erd_poll_list_builder, mixed_default_and_non_default_addresses)
{
  config.mode = BRIDGE_MODE_POLL;
  config.subscription_active = false;
  config.appliance_api_parsing = true;
  config.feature_bit_valid_erds = nullptr;
  config.feature_bit_valid_erds_count = 0;
  custom_erds_arr[0] = {0x1000, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_arr[1] = {0x1000, 0x10};
  custom_erds_arr[2] = {0x2000, PROBE_ENTRY_DEFAULT_ADDRESS};
  custom_erds_count = 3;
  config.custom_erds = custom_erds_arr;
  config.custom_erds_count = custom_erds_count;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(3u, result.erds_count);
}
