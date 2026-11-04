/*!
 * @file
 * @brief Unit tests for the ERD write bridge.
 */

extern "C" {
#include "erd_write_bridge.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/mqtt_client_double.hpp"
#include "simulation_test_base.h"
#include "tiny_gea_constants.h"

TEST_GROUP_BASE(erd_write_bridge, simulation_test_base)
{
  erd_write_bridge_t self;

  mqtt_client_double_t mqtt_client;

  void setup()
  {
    simulation_test_base_setup();
    mqtt_client_double_init(&mqtt_client);
  }

  tiny_gea3_erd_client_request_id_t mock_request_id;

  void teardown()
  {
    erd_write_bridge_destroy(&self);
    simulation_test_base_teardown();
  }

  void when_the_bridge_is_initialized(uint8_t host_address = 0xC0)
  {
    erd_write_bridge_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      host_address);
  }

  void given_that_the_bridge_has_been_initialized()
  {
    mock().disable();
    when_the_bridge_is_initialized();
    mock().enable();
  }

  void given_that_the_bridge_has_been_initialized_with_broadcast_address()
  {
    mock().disable();
    when_the_bridge_is_initialized(tiny_gea_broadcast_address);
    mock().enable();
  }

  void when_a_write_request_is_received(tiny_erd_t erd, const uint8_t* value, uint8_t size, uint8_t board_address = 0xFF)
  {
    mqtt_client_double_trigger_write_request(&mqtt_client, erd, board_address, size, value);
  }

  void when_a_write_is_completed(tiny_gea3_erd_client_request_id_t request_id, tiny_erd_t erd)
  {
    uint8_t dummy = 0;
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_write_completed;
    args.address = 0xC0;
    args.write_completed.request_id = request_id;
    args.write_completed.erd = erd;
    args.write_completed.data = &dummy;
    args.write_completed.data_size = 1;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void when_a_write_fails(tiny_gea3_erd_client_request_id_t request_id, tiny_erd_t erd,
    tiny_gea3_erd_client_write_failure_reason_t reason)
  {
    uint8_t dummy = 0;
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_write_failed;
    args.address = 0xC0;
    args.write_failed.request_id = request_id;
    args.write_failed.erd = erd;
    args.write_failed.data = &dummy;
    args.write_failed.data_size = 1;
    args.write_failed.reason = reason;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void should_report_write_result(tiny_erd_t erd, bool success,
    tiny_gea3_erd_client_write_failure_reason_t reason, uint8_t board_address = 0xFF)
  {
    mock()
      .expectOneCall("update_erd_write_result")
      .onObject(&mqtt_client)
      .withParameter("erd", erd)
      .withParameter("board_address", board_address)
      .withParameter("success", success)
      .withParameter("failure_reason", reason);
  }

  void expect_write_succeeds()
  {
    mock()
      .expectOneCall("write")
      .onObject(&erd_client)
      .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
      .ignoreOtherParameters()
      .andReturnValue(true);
  }
};

TEST(erd_write_bridge, should_forward_write_to_erd_client_when_host_address_is_known)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value = 0x01;
  expect_write_succeeds();
  when_a_write_request_is_received(0x3001, &value, sizeof(value));
}

TEST(erd_write_bridge, should_route_primary_board_write_to_host_address)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value = 0x01;
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", 0xC0)
    .withParameter("erd", 0x3001)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .ignoreOtherParameters()
    .andReturnValue(true);

  // board_address = 0xFF (primary) resolves to the detected host address 0xC0.
  when_a_write_request_is_received(0x3001, &value, sizeof(value), 0xFF);

  should_report_write_result(0x3001, true, 0, 0xFF);
  when_a_write_is_completed(mock_request_id, 0x3001);
}

TEST(erd_write_bridge, should_route_secondary_board_write_to_parsed_address)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value = 0x01;
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", 0xA1)
    .withParameter("erd", 0x3001)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .ignoreOtherParameters()
    .andReturnValue(true);

  // board_address = 0xA1 (secondary) routes to 0xA1, not the host address.
  when_a_write_request_is_received(0x3001, &value, sizeof(value), 0xA1);

  should_report_write_result(0x3001, true, 0, 0xA1);
  when_a_write_is_completed(mock_request_id, 0x3001);
}

TEST(erd_write_bridge, should_drop_write_when_host_address_is_broadcast)
{
  given_that_the_bridge_has_been_initialized_with_broadcast_address();

  uint8_t value = 0x01;
  should_report_write_result(0x3001, false, tiny_gea3_erd_client_write_failure_reason_not_supported);
  when_a_write_request_is_received(0x3001, &value, sizeof(value));
}

TEST(erd_write_bridge, should_report_write_success_to_mqtt)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value = 0x01;
  expect_write_succeeds();
  when_a_write_request_is_received(0x3001, &value, sizeof(value));

  should_report_write_result(0x3001, true, 0);
  when_a_write_is_completed(mock_request_id, 0x3001);
}

TEST(erd_write_bridge, should_report_write_failure_to_mqtt)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value = 0x01;
  expect_write_succeeds();
  when_a_write_request_is_received(0x3001, &value, sizeof(value));

  should_report_write_result(0x3001, false, tiny_gea3_erd_client_write_failure_reason_not_supported);
  when_a_write_fails(mock_request_id, 0x3001, tiny_gea3_erd_client_write_failure_reason_not_supported);
}

TEST(erd_write_bridge, should_drop_second_write_while_first_is_in_progress)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value1 = 0x01;
  expect_write_succeeds();
  when_a_write_request_is_received(0x3001, &value1, sizeof(value1));

  uint8_t value2 = 0x02;
  when_a_write_request_is_received(0x3002, &value2, sizeof(value2));

  should_report_write_result(0x3001, true, 0);
  when_a_write_is_completed(mock_request_id, 0x3001);
}

TEST(erd_write_bridge, should_handle_write_when_erd_client_queue_is_full)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value = 0x01;
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .ignoreOtherParameters()
    .andReturnValue(false);

  should_report_write_result(0x3001, false, tiny_gea3_erd_client_write_failure_reason_retries_exhausted);
  when_a_write_request_is_received(0x3001, &value, sizeof(value));
}

TEST(erd_write_bridge, should_handle_write_with_large_data)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value[] = {0x01, 0x02, 0x03, 0x04, 0x05};
  expect_write_succeeds();
  when_a_write_request_is_received(0x3001, value, sizeof(value));
}

TEST(erd_write_bridge, should_not_crash_on_destroy_without_init)
{
  erd_write_bridge_destroy(&self);
}

TEST(erd_write_bridge, should_handle_destroy_with_null_mqtt_client)
{
  self.timer_group = &timer_group.timer_group;
  self.mqtt_client = NULL;
  self.erd_client = NULL;

  erd_write_bridge_destroy(&self);
}