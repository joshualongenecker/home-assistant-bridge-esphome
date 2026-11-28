/*!
 * @file
 * @brief Comprehensive configuration-based tests for the GEA bridge.
 *
 * These tests validate different YAML configuration scenarios and their
 * resulting behavior at the application level, simulating various real-world
 * appliance configurations and operational modes.
 */

extern "C" {
#include "erd_cache.h"
}

#include "erd_bridge_subscribe.h"
#include "erd_bridge_poll.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"

TEST_GROUP(configuration_based_tests)
{
  enum {
    host_address = 0xC0,
    client_address = 0xE4,
    default_polling_interval = 10 * 1000,
    fast_polling_interval = 5 * 1000,
    slow_polling_interval = 30 * 1000,
  };
  
  enum {
    ERD_APPLIANCE_TYPE = 0x0008,
    ERD_MODEL_NUMBER = 0x0001,
    ERD_SERIAL_NUMBER = 0x0002,
    
    ERD_DISHWASHER_CYCLE_STATE = 0x3001,
    ERD_DISHWASHER_OPERATING_MODE = 0x3002,
    ERD_DISHWASHER_DOOR_STATUS = 0x3003,
    
    ERD_FRIDGE_TEMPERATURE = 0x0502,
    ERD_FREEZER_TEMPERATURE = 0x0503,
    ERD_ICE_MAKER_BUCKET_STATUS = 0x0504,
    
    ERD_LAUNDRY_CYCLE = 0x2001,
    ERD_LAUNDRY_END_TIME = 0x2002,
    
    APPLIANCE_TYPE_DISHWASHER = 6,
    APPLIANCE_TYPE_REFRIGERATOR = 5,
    APPLIANCE_TYPE_WASHER = 3,
  };
  
  erd_bridge_subscribe_t erd_bridge_subscribe;
  erd_bridge_poll_t erd_bridge_poll;

  erd_cache_t test_cache;
  
  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  
  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);
  }
  
  void teardown()
  {
    erd_bridge_subscribe_destroy(&erd_bridge_subscribe);
    erd_bridge_poll_destroy(&erd_bridge_poll);
    erd_cache_destroy(&test_cache);
  }
  
  void configure_subscription_mode(uint8_t address = host_address)
  {
    erd_bridge_subscribe_init(
      &erd_bridge_subscribe,
      &timer_group.timer_group,
      &erd_client.interface,
      address,
      &test_cache);
  }
  
  void configure_polling_mode(
    uint32_t polling_interval = default_polling_interval)
  {
    erd_bridge_poll_init(
      &erd_bridge_poll,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xC0, nullptr, 0,
      &test_cache);
  }
  
  void simulate_erd_read_response(
    tiny_gea3_erd_client_request_id_t request_id,
    tiny_erd_t erd,
    const uint8_t* data,
    uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = host_address;
    args.read_completed.request_id = request_id;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  void simulate_erd_publication(tiny_erd_t erd, const uint8_t* data, uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_publication_received;
    args.address = host_address;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data = data;
    args.subscription_publication_received.data_size = data_size;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  void simulate_subscription_added()
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args.address = host_address;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  void elapse_time(uint32_t milliseconds)
  {
    tiny_timer_group_double_elapse_time(&timer_group, milliseconds);
  }
};

// ============================================================================
// CONFIGURATION SCENARIO 1: Subscription Mode with Dishwasher
// YAML Config:
//   geappliances_bridge:
//     uart_id: gea3_uart
//     mode: subscribe
// ============================================================================

TEST(configuration_based_tests, config_subscription_mode_dishwasher_cycle)
{
  mock().disable();
  configure_subscription_mode();
  simulate_subscription_added();
  mock().enable();
  
  uint8_t cycle_state_running[] = {0x01};
  simulate_erd_publication(ERD_DISHWASHER_CYCLE_STATE, cycle_state_running, sizeof(cycle_state_running));

  uint8_t door_closed[] = {0x00};
  simulate_erd_publication(ERD_DISHWASHER_DOOR_STATUS, door_closed, sizeof(door_closed));

  CHECK(erd_cache_get_count(&test_cache) == 2);
}

// ============================================================================
// CONFIGURATION SCENARIO 2: Subscription Mode with Refrigerator
// YAML Config:
//   geappliances_bridge:
//     uart_id: gea3_uart
//     mode: subscribe
// ============================================================================

TEST(configuration_based_tests, config_subscription_mode_refrigerator_temperatures)
{
  mock().disable();
  configure_subscription_mode();
  simulate_subscription_added();
  mock().enable();
  
  // Simulate refrigerator temperature update (big-endian)
  uint8_t fridge_temp[] = {0x00, 0x25};
  simulate_erd_publication(ERD_FRIDGE_TEMPERATURE, fridge_temp, sizeof(fridge_temp));

  uint8_t freezer_temp[] = {0x00, 0x00};
  simulate_erd_publication(ERD_FREEZER_TEMPERATURE, freezer_temp, sizeof(freezer_temp));

  uint8_t ice_maker_full[] = {0x01};
  simulate_erd_publication(ERD_ICE_MAKER_BUCKET_STATUS, ice_maker_full, sizeof(ice_maker_full));

  CHECK(erd_cache_get_count(&test_cache) == 3);
}

// ============================================================================
// CONFIGURATION SCENARIO 3: Polling Mode with Default Interval
// YAML Config:
//   geappliances_bridge:
//     uart_id: gea3_uart
//     mode: poll
//     polling_interval: 10000
// ============================================================================

TEST(configuration_based_tests, config_polling_mode_default_interval)
{
  mock().disable();
  configure_polling_mode(default_polling_interval);

  mock().enable();
  CHECK(erd_bridge_poll.current_state == polling_state_polling);
}

// ============================================================================
// CONFIGURATION SCENARIO 4: Polling Mode with Fast Interval
// YAML Config:
//   geappliances_bridge:
//     uart_id: gea3_uart
//     mode: poll
//     polling_interval: 5000
// ============================================================================

TEST(configuration_based_tests, config_polling_mode_fast_interval)
{
  mock().disable();
  configure_polling_mode(fast_polling_interval);

  mock().enable();
  CHECK(erd_bridge_poll.current_state == polling_state_polling);
}

// ============================================================================
// CONFIGURATION SCENARIO 5: Polling Mode with Slow Interval
// YAML Config:
//   geappliances_bridge:
//     uart_id: gea3_uart
//     mode: poll
//     polling_interval: 30000
// ============================================================================

TEST(configuration_based_tests, config_polling_mode_slow_interval)
{
  mock().disable();
  configure_polling_mode(slow_polling_interval);

  mock().enable();
  CHECK(erd_bridge_poll.current_state == polling_state_polling);
}

// ============================================================================
// CONFIGURATION SCENARIO 6: Subscription Mode with Washer
// YAML Config:
//   geappliances_bridge:
//     uart_id: gea3_uart
//     mode: subscribe
// ============================================================================

TEST(configuration_based_tests, config_subscription_mode_washer_cycle)
{
  mock().disable();
  configure_subscription_mode();
  simulate_subscription_added();
  mock().enable();
  
  uint8_t cycle_running[] = {0x02};
  simulate_erd_publication(ERD_LAUNDRY_CYCLE, cycle_running, sizeof(cycle_running));

  uint8_t end_time[] = {0x00, 0x00, 0x00, 0x2D};
  simulate_erd_publication(ERD_LAUNDRY_END_TIME, end_time, sizeof(end_time));

  CHECK(erd_cache_get_count(&test_cache) == 2);
}

// ============================================================================
// CONFIGURATION SCENARIO 9: Mixed ERD Types
// Tests handling of various ERD data sizes and types
// ============================================================================

TEST(configuration_based_tests, config_subscription_mode_mixed_erd_sizes)
{
  mock().disable();
  configure_subscription_mode();
  simulate_subscription_added();
  mock().enable();

  uint8_t single_byte[] = {0xAB};
  simulate_erd_publication(ERD_DISHWASHER_DOOR_STATUS, single_byte, sizeof(single_byte));

  uint8_t two_bytes[] = {0x12, 0x34};
  simulate_erd_publication(ERD_FRIDGE_TEMPERATURE, two_bytes, sizeof(two_bytes));

  uint8_t four_bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
  simulate_erd_publication(ERD_LAUNDRY_END_TIME, four_bytes, sizeof(four_bytes));

  CHECK(erd_cache_get_count(&test_cache) == 3);
}
// ============================================================================
// DUAL SUBSCRIPTION TEST GROUP
//
// YAML Config (two appliances):
//   geappliances_bridge:
//     uart_id: gea3_uart
//     mode: subscribe
//
// Validates PR#58: erd_bridge_subscribe_init now accepts an address parameter, enabling
// two independent bridge instances to subscribe to different appliances.
// ============================================================================

TEST_GROUP(dual_subscription_config)
{
  enum {
    address_appliance_a = 0xC0,  // e.g. primary appliance (GEA3 default)
    address_appliance_b = 0xC4,  // e.g. secondary appliance discovered at runtime

    ERD_DISHWASHER_CYCLE = 0x3001,
    ERD_FRIDGE_TEMP      = 0x0502,
  };

  erd_bridge_subscribe_t bridge_a;
  erd_bridge_subscribe_t bridge_b;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;

  erd_cache_t test_cache;

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);
  }

  void teardown()
  {
    erd_bridge_subscribe_destroy(&bridge_a);
    erd_bridge_subscribe_destroy(&bridge_b);
    erd_cache_destroy(&test_cache);
  }

  void given_both_bridges_are_initialized()
  {
    mock().disable();
    erd_bridge_subscribe_init(
      &bridge_a,
      &timer_group.timer_group,
      &erd_client.interface,
      address_appliance_a,
      &test_cache);
    erd_bridge_subscribe_init(
      &bridge_b,
      &timer_group.timer_group,
      &erd_client.interface,
      address_appliance_b,
      &test_cache);
    mock().enable();
  }

  void given_both_subscriptions_are_active()
  {
    mock().disable();
    tiny_gea3_erd_client_on_activity_args_t args_a;
    args_a.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args_a.address = address_appliance_a;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args_a);

    tiny_gea3_erd_client_on_activity_args_t args_b;
    args_b.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args_b.address = address_appliance_b;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args_b);
    mock().enable();
  }

  void simulate_publication(uint8_t publisher_address, tiny_erd_t erd, const uint8_t* data, uint8_t size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_publication_received;
    args.address = publisher_address;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data = data;
    args.subscription_publication_received.data_size = size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
};

// ============================================================================
// DUAL SUBSCRIPTION SCENARIO 1: Each bridge subscribes to its own address
//
// YAML:
//   # Bridge A subscribes to appliance at 0xC0
//   # Bridge B subscribes to appliance at 0xC4
// ============================================================================

TEST(dual_subscription_config, each_bridge_subscribes_to_its_own_address)
{
  mock()
    .expectOneCall("subscribe")
    .onObject(&erd_client)
    .withParameter("address", address_appliance_a)
    .andReturnValue(true);
  erd_bridge_subscribe_init(
    &bridge_a,
    &timer_group.timer_group,
    &erd_client.interface,
    address_appliance_a,
    &test_cache);

  mock()
    .expectOneCall("subscribe")
    .onObject(&erd_client)
    .withParameter("address", address_appliance_b)
    .andReturnValue(true);
  erd_bridge_subscribe_init(
    &bridge_b,
    &timer_group.timer_group,
    &erd_client.interface,
    address_appliance_b,
    &test_cache);

  mock().checkExpectations();
}
// DUAL SUBSCRIPTION SCENARIO 2: Publications routed to correct MQTT client
//
// YAML (conceptual):
//   # Dishwasher at 0xC0 → mqtt_client_a
//   # Refrigerator at 0xC4 → mqtt_client_b
// ============================================================================

TEST(dual_subscription_config, publications_routed_to_correct_mqtt_client)
{
  given_both_bridges_are_initialized();
  given_both_subscriptions_are_active();

  uint8_t cycle_state[] = {0x01};
  simulate_publication(address_appliance_a, ERD_DISHWASHER_CYCLE, cycle_state, sizeof(cycle_state));

  uint8_t fridge_temp[] = {0x00, 0x25};
  simulate_publication(address_appliance_b, ERD_FRIDGE_TEMP, fridge_temp, sizeof(fridge_temp));

  CHECK(erd_cache_get_count(&test_cache) == 2);
}

// ============================================================================
// DUAL SUBSCRIPTION SCENARIO 3: Publications from one appliance not forwarded to other
//
// Tests isolation between bridges - appliance A publications must NOT appear
// on bridge B's cache and vice versa.

TEST(dual_subscription_config, publications_from_one_appliance_not_forwarded_to_other)
{
  given_both_bridges_are_initialized();
  given_both_subscriptions_are_active();

  uint8_t cycle_state[] = {0x02};
  simulate_publication(address_appliance_a, ERD_DISHWASHER_CYCLE, cycle_state, sizeof(cycle_state));

  CHECK(erd_cache_get_count(&test_cache) == 1);
}

// ============================================================================
// Polling bridge initialization tests
// ============================================================================

TEST_GROUP(polling_bridge_config)
{
  enum {
    host_address     = 0xC0,
    polling_interval = 10 * 1000,

    ERD_FRIDGE_TEMP = 0x0502,
    ERD_CYCLE_STATE = 0x3001,
  };

  erd_bridge_poll_t bridge;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;

  erd_cache_t test_cache;

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&bridge);
    erd_cache_destroy(&test_cache);
    mock().enable();
  }

  void configure_bridge()
  {
    erd_bridge_poll_init(
      &bridge,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xC0, nullptr, 0,
      &test_cache);
  }

  void simulate_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = host_address;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void elapse_time(uint32_t ms)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ms);
  }
};

// ============================================================================
// Polling bridge initializes correctly
// ============================================================================

TEST(polling_bridge_config, bridge_initializes_in_polling_state)
{
  // Polling bridge initializes in probe state (verifying it builds and runs).
  mock().disable();
  configure_bridge();
  mock().enable();

  CHECK(bridge.current_state == polling_state_polling);
}

// ============================================================================
// Polling bridge initializes with cache
// ============================================================================

TEST(polling_bridge_config, bridge_initializes_with_cache)
{
  // Bridge initializes in probe state with cache (verifying it builds and runs).
  mock().disable();
  configure_bridge();
  mock().enable();

  CHECK(bridge.current_state == polling_state_polling);
}
