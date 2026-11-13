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

#include <cstring>

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

  /* Expect one write on the bus with an exact payload, and hand back
   * `returned_id` as its request id. The expected payload lives in storage
   * owned by the test, so a queued write that replays a stale pointer to the
   * caller's buffer shows up as a mismatch here. */
  void expect_write(tiny_erd_t erd, uint8_t address, const uint8_t* value, uint8_t size,
    tiny_gea3_erd_client_request_id_t* returned_id)
  {
    mock()
      .expectOneCall("write")
      .onObject(&erd_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("data", value, size)
      .withOutputParameterReturning("request_id", returned_id, sizeof(*returned_id))
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

TEST(erd_write_bridge, should_queue_a_write_that_arrives_during_an_in_flight_write)
{
  given_that_the_bridge_has_been_initialized();

  tiny_gea3_erd_client_request_id_t first_id{1};
  tiny_gea3_erd_client_request_id_t second_id{2};
  uint8_t first_value = 0x01;
  uint8_t second_value = 0x02;

  // Expectations are declared in the order the calls actually happen: the
  // second write only reaches the bus after the first one has been reported.
  expect_write(0x3001, 0xC0, &first_value, sizeof(first_value), &first_id);
  should_report_write_result(0x3001, true, 0);
  expect_write(0x3002, 0xC0, &second_value, sizeof(second_value), &second_id);
  should_report_write_result(0x3002, true, 0);

  when_a_write_request_is_received(0x3001, &first_value, sizeof(first_value));
  when_a_write_request_is_received(0x3002, &second_value, sizeof(second_value));

  when_a_write_is_completed(first_id, 0x3001);
  when_a_write_is_completed(second_id, 0x3002);

  mock().checkExpectations();
}

TEST(erd_write_bridge, queued_write_sends_the_payload_copied_when_it_was_accepted)
{
  given_that_the_bridge_has_been_initialized();

  tiny_gea3_erd_client_request_id_t first_id{1};
  tiny_gea3_erd_client_request_id_t second_id{2};
  const uint8_t first_payload[2] = {0x11, 0x12};
  const uint8_t second_payload[2] = {0xAA, 0xBB};
  uint8_t caller_buffer[2];

  expect_write(0x3001, 0xC0, first_payload, sizeof(first_payload), &first_id);
  should_report_write_result(0x3001, true, 0);
  expect_write(0x3002, 0xC0, second_payload, sizeof(second_payload), &second_id);
  should_report_write_result(0x3002, true, 0);

  memcpy(caller_buffer, first_payload, sizeof(first_payload));
  when_a_write_request_is_received(0x3001, caller_buffer, sizeof(caller_buffer));

  memcpy(caller_buffer, second_payload, sizeof(second_payload));
  when_a_write_request_is_received(0x3002, caller_buffer, sizeof(caller_buffer));

  // The MQTT adapter reuses one stack buffer per write message, so the queued
  // request's buffer is gone before the queued write is ever sent.
  memset(caller_buffer, 0x00, sizeof(caller_buffer));

  when_a_write_is_completed(first_id, 0x3001);
  when_a_write_is_completed(second_id, 0x3002);

  mock().checkExpectations();
}

TEST(erd_write_bridge, should_report_queued_secondary_board_write_on_its_own_topic)
{
  given_that_the_bridge_has_been_initialized();

  tiny_gea3_erd_client_request_id_t first_id{1};
  tiny_gea3_erd_client_request_id_t second_id{2};
  uint8_t value = 0x01;

  expect_write(0x3001, 0xC0, &value, sizeof(value), &first_id);
  should_report_write_result(0x3001, true, 0, 0xFF);
  expect_write(0x3002, 0xA1, &value, sizeof(value), &second_id);
  should_report_write_result(0x3002, true, 0, 0xA1);

  when_a_write_request_is_received(0x3001, &value, sizeof(value), 0xFF);
  when_a_write_request_is_received(0x3002, &value, sizeof(value), 0xA1);

  when_a_write_is_completed(first_id, 0x3001);
  when_a_write_is_completed(second_id, 0x3002);

  mock().checkExpectations();
}

TEST(erd_write_bridge, a_stale_completion_publishes_nothing_and_starts_no_write)
{
  given_that_the_bridge_has_been_initialized();

  tiny_gea3_erd_client_request_id_t first_id{1};
  uint8_t value = 0x01;

  // The only call this test tolerates is the write that went out before the
  // stale event. Declaring no result expectation is the assertion: a
  // completion carrying an unexpected request id that consumed the in-flight
  // slot would show up as an unexpected publish or an unexpected second
  // write.
  expect_write(0x3001, 0xC0, &value, sizeof(value), &first_id);

  when_a_write_request_is_received(0x3001, &value, sizeof(value));
  when_a_write_request_is_received(0x3002, &value, sizeof(value));

  when_a_write_is_completed(99, 0x3001);

  mock().checkExpectations();
}

TEST(erd_write_bridge, should_still_drain_the_queue_after_a_stale_completion)
{
  given_that_the_bridge_has_been_initialized();

  tiny_gea3_erd_client_request_id_t first_id{1};
  tiny_gea3_erd_client_request_id_t second_id{2};
  uint8_t value = 0x01;

  expect_write(0x3001, 0xC0, &value, sizeof(value), &first_id);
  should_report_write_result(0x3001, true, 0);
  expect_write(0x3002, 0xC0, &value, sizeof(value), &second_id);
  should_report_write_result(0x3002, true, 0);

  when_a_write_request_is_received(0x3001, &value, sizeof(value));
  when_a_write_request_is_received(0x3002, &value, sizeof(value));

  when_a_write_is_completed(99, 0x3001);

  when_a_write_is_completed(first_id, 0x3001);
  when_a_write_is_completed(second_id, 0x3002);

  mock().checkExpectations();
}

TEST(erd_write_bridge, should_report_queued_write_that_fails_to_queue_and_keep_draining)
{
  given_that_the_bridge_has_been_initialized();

  tiny_gea3_erd_client_request_id_t first_id{1};
  tiny_gea3_erd_client_request_id_t third_id{3};
  uint8_t value = 0x01;

  expect_write(0x3001, 0xC0, &value, sizeof(value), &first_id);
  should_report_write_result(0x3001, true, 0);
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("erd", 0x3002)
    .ignoreOtherParameters()
    .andReturnValue(false);
  should_report_write_result(0x3002, false, tiny_gea3_erd_client_write_failure_reason_retries_exhausted);
  expect_write(0x3003, 0xC0, &value, sizeof(value), &third_id);
  should_report_write_result(0x3003, true, 0);

  when_a_write_request_is_received(0x3001, &value, sizeof(value));
  when_a_write_request_is_received(0x3002, &value, sizeof(value));
  when_a_write_request_is_received(0x3003, &value, sizeof(value));

  // Completing the first write drains 0x3002 (which the ERD client refuses)
  // and still puts 0x3003 on the bus.
  when_a_write_is_completed(first_id, 0x3001);
  when_a_write_is_completed(third_id, 0x3003);

  mock().checkExpectations();
}

TEST(erd_write_bridge, should_report_write_result_when_the_write_queue_is_full)
{
  given_that_the_bridge_has_been_initialized();

  tiny_gea3_erd_client_request_id_t first_id{1};
  uint8_t value = 0x01;

  // One write goes out and erd_write_bridge_queue_capacity more are queued;
  // the request after that is refused, and the refusal is published before
  // any queued write reaches the bus.
  expect_write(0x3001, 0xC0, &value, sizeof(value), &first_id);
  should_report_write_result(0x3006, false, tiny_gea3_erd_client_write_failure_reason_retries_exhausted);

  for(tiny_erd_t offset = 0; offset <= erd_write_bridge_queue_capacity; offset++) {
    when_a_write_request_is_received(0x3001 + offset, &value, sizeof(value));
  }
  when_a_write_request_is_received(0x3006, &value, sizeof(value));

  mock().checkExpectations();
}

TEST(erd_write_bridge, should_reject_write_with_payload_larger_than_the_limit)
{
  given_that_the_bridge_has_been_initialized();

  uint8_t value[ERD_WRITE_MAX_DATA_SIZE + 1] = {0};

  should_report_write_result(0x3001, false, tiny_gea3_erd_client_write_failure_reason_incorrect_size);
  when_a_write_request_is_received(0x3001, value, sizeof(value));

  mock().checkExpectations();
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