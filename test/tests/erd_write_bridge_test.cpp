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

  void when_the_bridge_is_initialized(uint8_t host_address = 0xC0, erd_cache_t* cache = nullptr)
  {
    erd_write_bridge_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      cache,
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

  // --- Field-level read-modify-write helpers ---
  // Uses the generated RMW table's matched pairs (0x7051->0x7053 1-byte,
  // 0x222d->0x222c 2-byte).

  void given_that_the_bridge_has_been_initialized_with_cache(uint8_t host_address = 0xC0)
  {
    mock().disable();
    when_the_bridge_is_initialized(host_address, &test_cache);
    mock().enable();
  }

  void given_that_the_base_is_cached(tiny_erd_t base_erd, const uint8_t* data, uint8_t size)
  {
    erd_cache_update(&test_cache, base_erd, 0xC0, data, size);
  }

  void when_a_field_write_is_received(tiny_erd_t erd, const uint8_t* value, uint8_t size,
    uint16_t byte_offset, uint8_t byte_size, uint8_t bit_offset, uint8_t bit_size,
    uint8_t board_address = 0xFF)
  {
    mqtt_client_double_trigger_field_write_request(&mqtt_client, erd, board_address, size, value,
      byte_offset, byte_size, bit_offset, bit_size);
  }

  void expect_rmw_write(tiny_erd_t erd, uint8_t address, const uint8_t* expected, uint8_t expected_size)
  {
    mock()
      .expectOneCall("write")
      .onObject(&erd_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("data", expected, expected_size)
      .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
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

TEST(erd_write_bridge, should_enable_writes_after_host_address_update)
{
  given_that_the_bridge_has_been_initialized_with_broadcast_address();

  erd_write_bridge_set_host_address(&self, 0xC0);

  uint8_t value = 0x01;
  expect_write_succeeds();
  when_a_write_request_is_received(0x3001, &value, sizeof(value));
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

// --- Field-level read-modify-write tests ---
// Uses the generated RMW table's matched pairs: 0x7051->0x7053 (1-byte) and
// 0x222d->0x222c (2-byte).

TEST(erd_write_bridge, should_rmw_bit_field_set_bit)
{
  given_that_the_bridge_has_been_initialized_with_cache();
  uint8_t base = 0x00;
  given_that_the_base_is_cached(0x7053, &base, sizeof(base));

  // Field: byte 0, bit 2, 1 bit. Value 0x01 (set) -> 0x00 | (1<<2) = 0x04.
  uint8_t expected = 0x04;
  expect_rmw_write(0x7051, 0xC0, &expected, sizeof(expected));

  uint8_t value = 0x01;
  when_a_field_write_is_received(0x7051, &value, sizeof(value), 0, 1, 2, 1);
}

TEST(erd_write_bridge, should_rmw_bit_field_clear_bit)
{
  given_that_the_bridge_has_been_initialized_with_cache();
  uint8_t base = 0x07; // bits 0,1,2 set
  given_that_the_base_is_cached(0x7053, &base, sizeof(base));

  // Field: byte 0, bit 2, 1 bit. Value 0x00 (clear) -> 0x07 & ~(1<<2) = 0x03.
  uint8_t expected = 0x03;
  expect_rmw_write(0x7051, 0xC0, &expected, sizeof(expected));

  uint8_t value = 0x00;
  when_a_field_write_is_received(0x7051, &value, sizeof(value), 0, 1, 2, 1);
}

TEST(erd_write_bridge, should_rmw_byte_aligned_field)
{
  given_that_the_bridge_has_been_initialized_with_cache();
  uint8_t base[] = {0xAA, 0xBB};
  given_that_the_base_is_cached(0x222c, base, sizeof(base));

  // Field: byte 1, size 1, bit 0, 8 bits. Value 0x42 -> {0xAA, 0x42}.
  uint8_t expected[] = {0xAA, 0x42};
  expect_rmw_write(0x222d, 0xC0, expected, sizeof(expected));

  uint8_t value = 0x42;
  when_a_field_write_is_received(0x222d, &value, sizeof(value), 1, 1, 0, 8);
}

TEST(erd_write_bridge, should_rmw_multi_bit_field)
{
  given_that_the_bridge_has_been_initialized_with_cache();
  uint8_t base = 0x00;
  given_that_the_base_is_cached(0x7053, &base, sizeof(base));

  // Field: byte 0, bit 1, 3 bits. Value 0b101 (0x05) -> bits 1,3 set = 0x0A.
  uint8_t expected = 0x0A;
  expect_rmw_write(0x7051, 0xC0, &expected, sizeof(expected));

  uint8_t value = 0x05;
  when_a_field_write_is_received(0x7051, &value, sizeof(value), 0, 1, 1, 3);
}

// Regression: a bit-field spanning multiple bytes must be placed big-endian
// within the field's byte_size span (matching the VT / appliance), not
// little-endian. 0x222d is a 2-byte RMW target. Bit 8 of the big-endian
// 2-byte integer is byte 0 bit 0 (the MSB byte), so setting it must touch
// byte 0, not byte 1.
TEST(erd_write_bridge, should_rmw_multi_byte_bit_field_high_bit)
{
  given_that_the_bridge_has_been_initialized_with_cache();
  uint8_t base[] = {0x00, 0x00};
  given_that_the_base_is_cached(0x222c, base, sizeof(base));

  // Field: byte 0, size 2, bit 8, 1 bit. Value 0x01 (set) -> byte 0 bit 0.
  uint8_t expected[] = {0x01, 0x00};
  expect_rmw_write(0x222d, 0xC0, expected, sizeof(expected));

  uint8_t value = 0x01;
  when_a_field_write_is_received(0x222d, &value, sizeof(value), 0, 2, 8, 1);
}

// Regression: a multi-bit field in a multi-byte span must read the rendered
// value big-endian (MSB first) AND place the bits big-endian within the span.
// 3-bit field at bit 5 of a 2-byte span, value 6 (0b110) rendered as
// {0x00, 0x06}: bits land in byte 1 bits 5,6,7 -> {0x00, 0xC0}.
TEST(erd_write_bridge, should_rmw_multi_byte_multi_bit_field)
{
  given_that_the_bridge_has_been_initialized_with_cache();
  uint8_t base[] = {0x00, 0x00};
  given_that_the_base_is_cached(0x222c, base, sizeof(base));

  // Field: byte 0, size 2, bit 5, 3 bits. Value {0x00, 0x06} (big-endian 6).
  uint8_t expected[] = {0x00, 0xC0};
  expect_rmw_write(0x222d, 0xC0, expected, sizeof(expected));

  uint8_t value[] = {0x00, 0x06};
  when_a_field_write_is_received(0x222d, value, sizeof(value), 0, 2, 5, 3);
}

TEST(erd_write_bridge, should_drop_field_write_when_base_missing)
{
  given_that_the_bridge_has_been_initialized_with_cache();
  // Base 0x7053 not cached.

  should_report_write_result(0x7051, false, tiny_gea3_erd_client_write_failure_reason_incorrect_size);
  uint8_t value = 0x01;
  when_a_field_write_is_received(0x7051, &value, sizeof(value), 0, 1, 2, 1);
}

TEST(erd_write_bridge, should_drop_field_write_when_base_wrong_size)
{
  given_that_the_bridge_has_been_initialized_with_cache();
  // Base 0x7053 cached with wrong size (2 bytes instead of 1).
  uint8_t base[] = {0x00, 0x00};
  given_that_the_base_is_cached(0x7053, base, sizeof(base));

  should_report_write_result(0x7051, false, tiny_gea3_erd_client_write_failure_reason_incorrect_size);
  uint8_t value = 0x01;
  when_a_field_write_is_received(0x7051, &value, sizeof(value), 0, 1, 2, 1);
}

TEST(erd_write_bridge, should_drop_field_write_when_not_rmw_target)
{
  given_that_the_bridge_has_been_initialized_with_cache();

  // 0x3001 is not in the RMW table.
  should_report_write_result(0x3001, false, tiny_gea3_erd_client_write_failure_reason_not_supported);
  uint8_t value = 0x01;
  when_a_field_write_is_received(0x3001, &value, sizeof(value), 0, 1, 0, 8);
}

TEST(erd_write_bridge, should_drop_field_write_when_no_cache)
{
  // Bridge initialized without a cache (nullptr) -> field writes dropped.
  given_that_the_bridge_has_been_initialized();

  should_report_write_result(0x7051, false, tiny_gea3_erd_client_write_failure_reason_not_supported);
  uint8_t value = 0x01;
  when_a_field_write_is_received(0x7051, &value, sizeof(value), 0, 1, 2, 1);
}

TEST(erd_write_bridge, should_full_write_unchanged_with_cache)
{
  // A full write (is_field_write=false) is forwarded verbatim even with a cache.
  given_that_the_bridge_has_been_initialized_with_cache();

  uint8_t value = 0x42;
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", 0xC0)
    .withParameter("erd", 0x3001)
    .withMemoryBufferParameter("data", &value, sizeof(value))
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_a_write_request_is_received(0x3001, &value, sizeof(value));
}

TEST(erd_write_bridge, should_drop_field_write_when_value_too_short)
{
  // A byte-aligned 2-byte field (0x222d -> 0x222c) with a 1-byte value must be
  // dropped: copying the fixed 2 bytes from a 1-byte value would read
  // uninitialized stack bytes into the appliance payload.
  given_that_the_bridge_has_been_initialized_with_cache();
  uint8_t base[] = {0xAA, 0xBB};
  given_that_the_base_is_cached(0x222c, base, sizeof(base));

  should_report_write_result(0x222d, false, tiny_gea3_erd_client_write_failure_reason_incorrect_size);
  uint8_t value = 0x42; // only 1 byte, but the field is 2 bytes
  when_a_field_write_is_received(0x222d, &value, sizeof(value), 0, 2, 0, 16);
}

TEST(erd_write_bridge, should_drop_field_write_when_value_too_long)
{
  // A byte-aligned 2-byte field (0x222d -> 0x222c) with a 3-byte value must be
  // dropped: copying the fixed 2 bytes from a 3-byte big-endian value would
  // silently MSB-truncate (copy the high-order bytes and discard the low-order
  // ones), writing a wrong value to the appliance.
  given_that_the_bridge_has_been_initialized_with_cache();
  uint8_t base[] = {0xAA, 0xBB};
  given_that_the_base_is_cached(0x222c, base, sizeof(base));

  should_report_write_result(0x222d, false, tiny_gea3_erd_client_write_failure_reason_incorrect_size);
  uint8_t value[] = {0x00, 0x01, 0x00}; // 3 bytes, but the field is 2 bytes
  when_a_field_write_is_received(0x222d, value, sizeof(value), 0, 2, 0, 16);
}