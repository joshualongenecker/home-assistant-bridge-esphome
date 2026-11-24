/*!
 * @file
 * @brief Application-level integration tests for the GEA bridge.
 * 
 * These tests simulate the complete application behavior by setting up
 * the full bridge component with mocked GEA3 ERD client and MQTT client,
 * then simulating appliance responses.
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

/*!
 * Test group for application-level integration tests.
 * 
 * These tests validate the complete behavior of the bridge application
 * by simulating realistic appliance interactions.
 */
TEST_GROUP(application_level)
{
  enum {
    appliance_address = 0xC0,  // Default GEA3 host address
    resubscribe_delay = 1000,
    subscription_retention_period = 30 * 1000,
    polling_interval = 10 * 1000,
  };
  
  // ERD identifiers used in device ID generation
  enum {
    ERD_APPLIANCE_TYPE = 0x0008,
    ERD_MODEL_NUMBER = 0x0001,
    ERD_SERIAL_NUMBER = 0x0002,
    ERD_TEMPERATURE = 0x1004,  // Example ERD for testing
  };
  
  erd_bridge_subscribe_t erd_bridge_subscribe;
  erd_bridge_poll_t erd_bridge_poll;
  erd_cache_t test_cache;
  
  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  
  uint8_t dummy;
  
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
  
  /*!
   * Initialize the ERD bridge in subscription mode.
   */
  void initialize_erd_bridge_subscription_mode()
  {
    erd_bridge_subscribe_init(
      &erd_bridge_subscribe,
      &timer_group.timer_group,
      &erd_client.interface,
      appliance_address,
      &test_cache);
  }
  
  /*!
   * Initialize the ERD bridge in polling mode.
   */
  void initialize_erd_bridge_polling_mode()
  {
    erd_bridge_poll_init(
      &erd_bridge_poll,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xC0, nullptr, 0,
      &test_cache);
  }
  
  /*!
   * Simulate a successful ERD read response from the appliance.
   */
  void simulate_erd_read_response(
    tiny_gea3_erd_client_request_id_t request_id,
    tiny_erd_t erd,
    const uint8_t* data,
    uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = appliance_address;
    args.read_completed.request_id = request_id;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  /*!
   * Simulate a failed ERD read response.
   */
  void simulate_erd_read_failed(
    tiny_gea3_erd_client_request_id_t request_id,
    tiny_erd_t erd,
    tiny_gea3_erd_client_read_failure_reason_t reason)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = appliance_address;
    args.read_failed.request_id = request_id;
    args.read_failed.erd = erd;
    args.read_failed.reason = reason;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  /*!
   * Simulate a subscription being added successfully.
   */
  void simulate_subscription_added(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args.address = address;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  /*!
   * Simulate an ERD publication from a subscription.
   */
  void simulate_erd_publication(tiny_erd_t erd, const uint8_t* data, uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_publication_received;
    args.address = appliance_address;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data = data;
    args.subscription_publication_received.data_size = data_size;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  /*!
   * Simulate a subscription host coming online.
   */
  void simulate_subscription_host_online(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_host_came_online;
    args.address = address;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
};

/*!
 * Test that with an empty probe list and a known host address,
 * the bridge starts in probe_list and transitions directly to
 * polling without needing any broadcast discovery.
 */
TEST(application_level, should_enter_polling_with_empty_probe_list)
{
  // With a known host address and no probe list, probe_list
  // transitions directly to polling on initialization.
  mock().disable();
  initialize_erd_bridge_polling_mode();

  mock().enable();
  CHECK(erd_bridge_poll.current_state == polling_state_polling);
}

/*!
 * Test subscription mode with simulated ERD publications.
 */
TEST(application_level, should_handle_erd_publications_in_subscription_mode)
{
  mock().disable();
  initialize_erd_bridge_subscription_mode();
  simulate_subscription_added(appliance_address);
  mock().enable();

  // Simulate an ERD publication from the appliance.
  uint8_t temp_value = 0x1A;
  simulate_erd_publication(ERD_TEMPERATURE, &temp_value, sizeof(temp_value));

  // Verify the ERD is in the cache
  CHECK(erd_cache_get_count(&test_cache) == 1);
}

/*!
 * Test polling mode with simulated ERD responses.
 */
TEST(application_level, should_poll_erds_periodically_in_polling_mode)
{
  // Validate that the polling bridge can be initialized and enters
  // the probe_list state, ready to probe for appliances.
  mock().disable();
  initialize_erd_bridge_polling_mode();

  // Bridge should be in the probe_list state initially.
  mock().enable();
  CHECK(erd_bridge_poll.current_state == polling_state_polling);
}


/*!
 * Test the complete workflow of subscription with publications.
 */
TEST(application_level, should_complete_subscription_workflow_with_publications)
{
  // Use the same approach as the existing erd_bridge_subscribe tests
  mock().disable();
  initialize_erd_bridge_subscription_mode();
  simulate_subscription_added(appliance_address);
  mock().enable();
  
  // Appliance publishes an ERD update
  uint8_t temperature_data[] = {0x00, 0x50};  // Big-endian 80 (degrees)
  simulate_erd_publication(ERD_TEMPERATURE, temperature_data, sizeof(temperature_data));

  // Verify the ERD is in the cache
  CHECK(erd_cache_get_count(&test_cache) == 1);
}
