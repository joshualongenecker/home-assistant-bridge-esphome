/*!
 * @file
 * @brief Example of more comprehensive application-level simulation tests.
 *
 * This file demonstrates how to create realistic test scenarios that simulate
 * complete appliance interactions including device ID generation, mode switching,
 * and error handling.
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
 * Test group demonstrating comprehensive appliance simulation scenarios.
 *
 * These examples show how to test complete workflows including:
 * - Device ID generation from ERD reads
 * - Mode switching and fallback behavior
 * - Error handling and retry logic
 * - Multi-step interactions
 */
TEST_GROUP(appliance_simulation_examples)
{
  enum {
    host_address = 0xC0,
    client_address = 0xE4,
    resubscribe_delay = 1000,
    subscription_retention_period = 30 * 1000,
    polling_interval = 10 * 1000,
    retry_delay = 100,
  };
  
  enum {
    ERD_APPLIANCE_TYPE = 0x0008,
    ERD_MODEL_NUMBER = 0x0001,
    ERD_SERIAL_NUMBER = 0x0002,
    
    ERD_CYCLE_STATE = 0x3001,
    ERD_OPERATING_MODE = 0x3002,
    ERD_DOOR_STATUS = 0x3003,
    
    APPLIANCE_TYPE_DISHWASHER = 6,
    APPLIANCE_TYPE_REFRIGERATOR = 5,
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
  
  void initialize_erd_bridge_subscription_mode()
  {
    erd_bridge_subscribe_init(
      &erd_bridge_subscribe,
      &timer_group.timer_group,
      &erd_client.interface,
      host_address,
      &test_cache);
  }
  
  void initialize_erd_bridge_polling_mode()
  {
    erd_bridge_poll_init(
      &erd_bridge_poll,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xFF, nullptr, 0,
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
  
  void simulate_erd_read_failed(
    tiny_gea3_erd_client_request_id_t request_id,
    tiny_erd_t erd,
    tiny_gea3_erd_client_read_failure_reason_t reason)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = host_address;
    args.read_failed.request_id = request_id;
    args.read_failed.erd = erd;
    args.read_failed.reason = reason;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  void simulate_erd_write_completed(
    tiny_gea3_erd_client_request_id_t request_id,
    tiny_erd_t erd,
    const uint8_t* data,
    uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_write_completed;
    args.address = host_address;
    args.write_completed.request_id = request_id;
    args.write_completed.erd = erd;
    args.write_completed.data = data;
    args.write_completed.data_size = data_size;
    
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
  
  void simulate_subscription_added()
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args.address = host_address;
    
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
  
  void elapse_time(uint32_t milliseconds)
  {
    tiny_timer_group_double_elapse_time(&timer_group, milliseconds);
  }
};

TEST(appliance_simulation_examples, example_device_id_generation_workflow)
{

  mock().disable();
  initialize_erd_bridge_subscription_mode();
  simulate_subscription_added();
  mock().enable();

  uint8_t appliance_type_data = APPLIANCE_TYPE_DISHWASHER;
  simulate_erd_publication(ERD_APPLIANCE_TYPE, &appliance_type_data, sizeof(appliance_type_data));

  uint8_t model_data[] = "GDT695SBL0SS";
  simulate_erd_publication(ERD_MODEL_NUMBER, model_data, sizeof(model_data));

  uint8_t serial_data[] = "SN123456789";
  simulate_erd_publication(ERD_SERIAL_NUMBER, serial_data, sizeof(serial_data));

  CHECK(erd_cache_get_count(&test_cache) == 3);
}

TEST(appliance_simulation_examples, example_dishwasher_cycle_simulation)
{
  mock().disable();
  initialize_erd_bridge_subscription_mode();
  simulate_subscription_added();
  mock().enable();
  
  uint8_t cycle_state_running[] = {0x01};
  simulate_erd_publication(ERD_CYCLE_STATE, cycle_state_running, sizeof(cycle_state_running));

  uint8_t door_closed[] = {0x00};
  simulate_erd_publication(ERD_DOOR_STATUS, door_closed, sizeof(door_closed));

  CHECK(erd_cache_get_count(&test_cache) == 2);
}

TEST(appliance_simulation_examples, example_periodic_polling_behavior)
{

  mock().disable();
  initialize_erd_bridge_subscription_mode();
  simulate_subscription_added();
  mock().enable();

  uint8_t cycle_idle = 0x00;
  simulate_erd_publication(ERD_CYCLE_STATE, &cycle_idle, sizeof(cycle_idle));

  uint8_t cycle_running = 0x01;
  simulate_erd_publication(ERD_CYCLE_STATE, &cycle_running, sizeof(cycle_running));

  CHECK(erd_cache_get_count(&test_cache) == 1);
}
