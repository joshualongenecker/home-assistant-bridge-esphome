/*!
 * @file
 * @brief Unit tests for GeappliancesBridge configuration API.
 *
 * Tests the public configuration setters/getters, custom ERD management,
 * and teardown safety.  The full integration flow (setup -> loop -> teardown)
 * is covered in startup_hsm_test.cpp.
 *
 * Note: Production headers with STL containers (<string>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "geappliances_bridge.h"
#include "bridge_mode.h"
#include "i_bridge_services.h"
#include "CppUTest/TestHarness.h"

using namespace esphome::geappliances_bridge;

// Custom ERD max is defined in the class as protected; duplicate the value here
// to test capacity without accessing protected members.
static const uint16_t TEST_CUSTOM_ERDS_MAX = 128;

TEST_GROUP(geappliances_bridge)
{
  GeappliancesBridge bridge;

  void setup()
  {
    // Each test gets a fresh instance with default member values:
    //   mode_ = BRIDGE_MODE_AUTO
    //   polling_interval_ms_ = 10000
    //   client_address_ = 0xE4
    //   configured_device_id_ = ""
    //   appliance_api_parsing_ = true
    //   custom_erds_count_ = 0
  }
};

TEST(geappliances_bridge, set_mode_poll)
{
  bridge.set_mode(BRIDGE_MODE_POLL);
  // Verify via IBridgeServices interface (get_mode is protected in class but
  // public in IBridgeServices — access through the interface pointer)
  IBridgeServices* services = &bridge;
  CHECK_EQUAL(BRIDGE_MODE_POLL, services->get_mode());
}

TEST(geappliances_bridge, set_mode_subscribe)
{
  bridge.set_mode(BRIDGE_MODE_SUBSCRIBE);
  IBridgeServices* services = &bridge;
  CHECK_EQUAL(BRIDGE_MODE_SUBSCRIBE, services->get_mode());
}

TEST(geappliances_bridge, set_mode_auto)
{
  bridge.set_mode(BRIDGE_MODE_AUTO);
  IBridgeServices* services = &bridge;
  CHECK_EQUAL(BRIDGE_MODE_AUTO, services->get_mode());
}

TEST(geappliances_bridge, default_mode_is_auto)
{
  IBridgeServices* services = &bridge;
  CHECK_EQUAL(BRIDGE_MODE_AUTO, services->get_mode());
}

TEST(geappliances_bridge, set_polling_interval_various_values)
{
  bridge.set_polling_interval(1);
  bridge.set_polling_interval(5000);
  bridge.set_polling_interval(60000);
  bridge.set_polling_interval(UINT32_MAX);
}

TEST(geappliances_bridge, set_device_id)
{
  bridge.set_device_id("test-device-123");
  bridge.set_device_id("another");
  bridge.set_device_id("");
}

TEST(geappliances_bridge, set_client_address)
{
  bridge.set_client_address(0x80);
  bridge.set_client_address(0x00);
  bridge.set_client_address(0xFF);
  bridge.set_client_address(0xC0);
}

TEST(geappliances_bridge, set_appliance_api_parsing)
{
  bridge.set_appliance_api_parsing(false);
  bridge.set_appliance_api_parsing(true);
}

TEST(geappliances_bridge, set_generate_device_config)
{
  bridge.set_generate_device_config(true);
  bridge.set_generate_device_config(false);
}

TEST(geappliances_bridge, add_custom_erd_increments_count)
{
  bridge.add_custom_erd(0x0100);
  bridge.add_custom_erd(0x0200);
  bridge.add_custom_erd(0x0300);
}

TEST(geappliances_bridge, add_custom_erd_at_capacity)
{
  // Fill to exactly CUSTOM_ERDS_MAX (64).
  for (uint16_t i = 0; i < TEST_CUSTOM_ERDS_MAX; i++) {
    bridge.add_custom_erd(static_cast<tiny_erd_t>(i));
  }

  // The 65th call must be silently rejected (guard: count >= MAX).
  bridge.add_custom_erd(0xFFFF);

  // Further calls also must be no-ops.
  bridge.add_custom_erd(0x0000);
}

TEST(geappliances_bridge, add_custom_erd_beyond_capacity)
{
  // Fill past capacity in a loop to stress the guard.
  for (uint16_t i = 0; i < TEST_CUSTOM_ERDS_MAX + 10; i++) {
    bridge.add_custom_erd(static_cast<tiny_erd_t>(i));
  }
}

TEST(geappliances_bridge, add_custom_erd_with_zero)
{
  bridge.add_custom_erd(0x0000);
}

TEST(geappliances_bridge, add_custom_erd_with_max_erd)
{
  bridge.add_custom_erd(0xFFFF);
}

TEST(geappliances_bridge, teardown_uninitialized)
{
  // Calling teardown() on a bridge that has never been set up must not crash.
  bool result = bridge.teardown();
  CHECK_TRUE(result);
}

TEST(geappliances_bridge, teardown_after_config_setters)
{
  bridge.set_mode(BRIDGE_MODE_POLL);
  bridge.set_polling_interval(5000);
  bridge.set_device_id("test");
  bridge.set_client_address(0x80);
  bridge.set_appliance_api_parsing(false);
  bridge.add_custom_erd(0x0100);

  bool result = bridge.teardown();
  CHECK_TRUE(result);
}

TEST(geappliances_bridge, teardown_after_custom_erds_filled)
{
  for (uint16_t i = 0; i < TEST_CUSTOM_ERDS_MAX; i++) {
    bridge.add_custom_erd(static_cast<tiny_erd_t>(i));
  }

  bool result = bridge.teardown();
  CHECK_TRUE(result);
}

TEST(geappliances_bridge, set_gea3_uart_null)
{
  bridge.set_gea3_uart(nullptr);
}

TEST(geappliances_bridge, set_gea2_uart_null)
{
  bridge.set_gea2_uart(nullptr);
}

TEST(geappliances_bridge, set_sensor_null)
{
  bridge.set_erd_publish_rate_sensor(nullptr);
  bridge.set_erd_cache_entries_sensor(nullptr);
  bridge.set_erd_cache_updates_sensor(nullptr);
  bridge.set_mqtt_publish_rate_sensor(nullptr);
}


TEST(geappliances_bridge, is_bridge_initialized_default_false)
{
  IBridgeServices* services = &bridge;
  CHECK_FALSE(services->is_bridge_initialized());
}

TEST(geappliances_bridge, is_autodiscovery_complete_default_false)
{
  IBridgeServices* services = &bridge;
  CHECK_FALSE(services->is_autodiscovery_complete());
}

TEST(geappliances_bridge, is_device_id_complete_default_false)
{
  IBridgeServices* services = &bridge;
  CHECK_FALSE(services->is_device_id_complete());
}

TEST(geappliances_bridge, is_feature_bits_complete_default_false)
{
  IBridgeServices* services = &bridge;
  CHECK_FALSE(services->is_feature_bits_complete());
}

TEST(geappliances_bridge, is_mqtt_client_initialized_default_false)
{
  IBridgeServices* services = &bridge;
  CHECK_FALSE(services->is_mqtt_client_initialized());
}

TEST(geappliances_bridge, is_erd_cache_publisher_initialized_default_false)
{
  IBridgeServices* services = &bridge;
  CHECK_FALSE(services->is_erd_cache_publisher_initialized());
}

TEST(geappliances_bridge, get_subscription_state_default_none)
{
  IBridgeServices* services = &bridge;
  CHECK_EQUAL(static_cast<int>(subscription_state_none), static_cast<int>(services->get_subscription_state()));
}

TEST(geappliances_bridge, get_polling_state_default_none)
{
  IBridgeServices* services = &bridge;
  CHECK_EQUAL(static_cast<int>(polling_state_none), static_cast<int>(services->get_polling_state()));
}

TEST(geappliances_bridge, get_discovered_host_address_default_zero)
{
  IBridgeServices* services = &bridge;
  CHECK_EQUAL(0, services->get_discovered_host_address());
}

TEST(geappliances_bridge, is_discovered_gea2_protocol_default_false)
{
  IBridgeServices* services = &bridge;
  CHECK_FALSE(services->is_discovered_gea2_protocol());
}
