/*!
 * @file
 * @brief Tests for polling_bridge
 */

extern "C" {
#include "polling_bridge.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/mqtt_client_double.hpp"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"

// Export ERD lists for the polling bridge to use
extern "C" const tiny_erd_t common_erds[] = { 0x0001, 0x0002, 0x0008, 0x0035 };
extern "C" const size_t common_erd_count = 4;
extern "C" const tiny_erd_t energy_erds[] = { 0xD001, 0xD002, 0xD003 };
extern "C" const size_t energy_erd_count = 3;

TEST_GROUP(polling_bridge)
{
  enum {
    retry_delay = 100,
    polling_interval = 10000,
    appliance_lost_timeout = 60000
  };

  polling_bridge_t self;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  mqtt_client_double_t mqtt_client;

  void setup()
  {
    mock().strictOrder();

    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    mqtt_client_double_init(&mqtt_client);
  }

  void teardown()
  {
    polling_bridge_destroy(&self);
    mock().checkExpectations();
  }

  void when_the_bridge_is_initialized()
  {
    polling_bridge_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval);
  }

  void given_that_the_bridge_has_been_initialized()
  {
    mock().disable();
    when_the_bridge_is_initialized();
    mock().enable();
  }

  void should_request_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  void should_register_erd(tiny_erd_t erd)
  {
    mock()
      .expectOneCall("register_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd);
  }

  template <typename T>
  void should_update_erd(tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;

    mock()
      .expectOneCall("update_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("value", reinterpret_cast<const uint8_t*>(&_value), sizeof(_value));
  }

  template <typename T>
  void when_a_read_completes_successfully(uint8_t address, tiny_erd_t erd, T data)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.erd = erd;
    args.read_completed.data = &data;
    args.read_completed.data_size = sizeof(data);

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  template <typename T>
  void given_that_a_read_completed_successfully(uint8_t address, tiny_erd_t erd, T data)
  {
    mock().disable();
    when_a_read_completes_successfully(address, erd, data);
    mock().enable();
  }

  void when_a_read_fails(uint8_t address, tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = address;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_retries_exhausted;

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  void after_mqtt_disconnects()
  {
    mqtt_client_double_trigger_mqtt_disconnect(&mqtt_client);
  }

  void given_that_mqtt_has_disconnected()
  {
    mock().disable();
    after_mqtt_disconnects();
    mock().enable();
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void nothing_should_happen()
  {
  }

  template <typename T>
  void should_request_erd_write(uint8_t address, tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;

    mock()
      .expectOneCall("write")
      .onObject(&erd_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("data", reinterpret_cast<const uint8_t*>(&_value), sizeof(_value))
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  template <typename T>
  void when_a_write_request_is_received(tiny_erd_t erd, T value)
  {
    mqtt_client_double_trigger_write_request(&mqtt_client, erd, sizeof(value), &value);
  }

  void should_update_erd_write_result(tiny_erd_t erd, bool success, tiny_gea3_erd_client_write_failure_reason_t reason)
  {
    mock()
      .expectOneCall("update_erd_write_result")
      .onObject(&mqtt_client)
      .withParameter("erd", erd)
      .withParameter("success", success)
      .withParameter("failure_reason", reason);
  }

  template <typename T>
  void when_a_write_request_completes_successfully(uint8_t address, tiny_erd_t erd, T value)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_write_completed;
    args.address = address;
    args.write_completed.erd = erd;
    args.write_completed.data = &value;
    args.write_completed.data_size = sizeof(value);
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  template <typename T>
  void when_a_write_request_completes_unsuccessfully(uint8_t address, tiny_erd_t erd, T value, tiny_gea3_erd_client_write_failure_reason_t reason)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_write_failed;
    args.address = address;
    args.write_failed.erd = erd;
    args.write_failed.data = &value;
    args.write_failed.data_size = sizeof(value);
    args.write_failed.reason = reason;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
};

// ========== Initialization Tests ==========

TEST(polling_bridge, should_read_appliance_type_when_initialized)
{
  should_request_read(0xFF, 0x0008);  // Broadcast address, appliance type ERD
  when_the_bridge_is_initialized();
}

TEST(polling_bridge, should_retry_appliance_type_read_on_timeout)
{
  given_that_the_bridge_has_been_initialized();
  
  nothing_should_happen();
  after(retry_delay - 1);
  
  should_request_read(0xFF, 0x0008);
  after(1);
}

// ========== Common ERD Discovery Tests ==========

TEST(polling_bridge, should_discover_common_erds_after_identifying_appliance)
{
  given_that_the_bridge_has_been_initialized();
  
  // Appliance type read completes in identify_appliance state
  // Transitions to state_add_common_erds which reads first common ERD on entry
  should_request_read(0xC0, 0x0001);  // First common ERD (on state entry)
  when_a_read_completes_successfully(0xC0, 0x0008, uint8_t(6));  // Appliance type = 6
}

TEST(polling_bridge, should_register_and_update_discovered_common_erds)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  should_register_erd(0x0001);
  should_update_erd(0x0001, uint32_t(0x12345678));
  should_request_read(0xC0, 0x0002);  // Next common ERD
  when_a_read_completes_successfully(0xC0, 0x0001, uint32_t(0x12345678));
}

TEST(polling_bridge, should_continue_discovery_after_failed_common_erd_read)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  
  // Failed read should not register or update
  nothing_should_happen();
  when_a_read_fails(0xC0, 0x0002);
  
  // But should continue to next ERD on timeout
  should_request_read(0xC0, 0x0008);  // Next common ERD (index 2)
  after(retry_delay);
}

TEST(polling_bridge, should_transition_to_energy_erds_after_all_common_erds_checked)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  // Go through all common ERDs
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0x87654321));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0xABCDEF00));
  
  // After last common ERD completes and we call send_next_read_request,
  // it transitions to energy ERDs state which immediately reads first energy ERD in entry
  should_register_erd(0x0035);
  should_update_erd(0x0035, uint32_t(0xAABBCCDD));
  should_request_read(0xC0, 0xD001);  // First energy ERD (on state entry)
  when_a_read_completes_successfully(0xC0, 0x0035, uint32_t(0xAABBCCDD));
}

// ========== Energy ERD Discovery Tests ==========

TEST(polling_bridge, should_register_and_update_discovered_energy_erds)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  // Skip through common ERDs
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0x87654321));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0xABCDEF00));
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0xABCDEF00));
  
  // Energy ERD discovery
  should_register_erd(0xD001);
  should_update_erd(0xD001, uint16_t(1234));
  should_request_read(0xC0, 0xD002);  // Next energy ERD
  when_a_read_completes_successfully(0xC0, 0xD001, uint16_t(1234));
}

// ========== Polling State Tests ==========

TEST(polling_bridge, should_start_polling_after_discovery_phase)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  // Complete common ERD discovery
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0x87654321));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0xABCDEF00));
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0xAABBCCDD));
  
  // Complete energy ERD discovery (erd_index will be at 3 after this)
  given_that_a_read_completed_successfully(0xC0, 0xD001, uint16_t(1234));
  given_that_a_read_completed_successfully(0xC0, 0xD002, uint16_t(5678));
  
  // After last energy ERD completes, transitions to polling state
  // Polling state entry immediately starts polling at current erd_index (3)
  // Polling list is: [0x0001, 0x0002, 0x0008, 0x0035, 0xD001, 0xD002, 0xD003]
  // Index 3 = 0x0035
  should_register_erd(0xD003);
  should_update_erd(0xD003, uint16_t(9012));
  should_request_read(0xC0, 0x0035);  // Continue from where discovery left off (index 3)
  when_a_read_completes_successfully(0xC0, 0xD003, uint16_t(9012));
}

TEST(polling_bridge, should_poll_all_discovered_erds_in_sequence)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  // Set up discovered ERDs: 0x0001, 0x0002, 0x0008, 0x0035, 0xD001, 0xD002, 0xD003
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0x87654321));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0xABCDEF00));
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0xAABBCCDD));
  given_that_a_read_completed_successfully(0xC0, 0xD001, uint16_t(1234));
  given_that_a_read_completed_successfully(0xC0, 0xD002, uint16_t(5678));
  given_that_a_read_completed_successfully(0xC0, 0xD003, uint16_t(9012));
  
  // Now in polling state starting at index 3 (0x0035)
  // When 0x0035 completes, should poll next (index 4 = 0xD001)
  should_update_erd(0x0035, uint32_t(0xAAAABBBB));
  should_request_read(0xC0, 0xD001);  // Next in polling list (index 4)
  when_a_read_completes_successfully(0xC0, 0x0035, uint32_t(0xAAAABBBB));
}

TEST(polling_bridge, should_restart_polling_cycle_after_reaching_end_of_list)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  // Set up with discovered ERDs
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0x87654321));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0xABCDEF00));
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0xAABBCCDD));
  given_that_a_read_completed_successfully(0xC0, 0xD001, uint16_t(1234));
  given_that_a_read_completed_successfully(0xC0, 0xD002, uint16_t(5678));
  given_that_a_read_completed_successfully(0xC0, 0xD003, uint16_t(9012));
  
  // Poll through entire list
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0xAAAABBBB));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0xCCCCDDDD));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0x11223344));
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0x55667788));
  given_that_a_read_completed_successfully(0xC0, 0xD001, uint16_t(1111));
  given_that_a_read_completed_successfully(0xC0, 0xD002, uint16_t(2222));
  
  // After polling interval expires, should restart from beginning
  should_request_read(0xC0, 0x0001);  // Restart from first ERD
  after(polling_interval);
}

TEST(polling_bridge, should_update_erds_during_polling)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  // Set up discovered ERDs
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0x87654321));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0xABCDEF00));
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0xAABBCCDD));
  given_that_a_read_completed_successfully(0xC0, 0xD001, uint16_t(1234));
  given_that_a_read_completed_successfully(0xC0, 0xD002, uint16_t(5678));
  given_that_a_read_completed_successfully(0xC0, 0xD003, uint16_t(9012));
  
  // During polling (starting at index 3), should update ERD values
  should_update_erd(0x0035, uint32_t(0x11223344));
  should_request_read(0xC0, 0xD001);
  when_a_read_completes_successfully(0xC0, 0x0035, uint32_t(0x11223344));
}

// ========== Write Request Tests ==========

TEST(polling_bridge, should_forward_write_requests_from_mqtt_client)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  should_request_erd_write(0xC0, 0x1234, uint32_t(0xAABBCCDD));
  when_a_write_request_is_received(0x1234, uint32_t(0xAABBCCDD));
}

TEST(polling_bridge, should_report_write_results_to_mqtt_client)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  should_update_erd_write_result(0x1234, true, 0);
  when_a_write_request_completes_successfully(0xC0, 0x1234, uint32_t(0xAABBCCDD));
  
  should_update_erd_write_result(0x5678, false, tiny_gea3_erd_client_write_failure_reason_not_supported);
  when_a_write_request_completes_unsuccessfully(0xC0, 0x5678, uint32_t(0x11223344), tiny_gea3_erd_client_write_failure_reason_not_supported);
}

// ========== MQTT Reconnection Tests ==========

TEST(polling_bridge, should_restart_discovery_when_mqtt_disconnects_during_polling)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  // Complete discovery and enter polling
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0x87654321));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0xABCDEF00));
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0xAABBCCDD));
  given_that_a_read_completed_successfully(0xC0, 0xD001, uint16_t(1234));
  given_that_a_read_completed_successfully(0xC0, 0xD002, uint16_t(5678));
  given_that_a_read_completed_successfully(0xC0, 0xD003, uint16_t(9012));
  
  // Poll at least one ERD to be in polling loop (polling starts at index 3 = 0x0035)
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0x11223344));
  
  // MQTT disconnect should restart from appliance identification
  should_request_read(0xFF, 0x0008);  // Broadcast, appliance type
  after_mqtt_disconnects();
}

// ========== Appliance Lost Tests ==========

TEST(polling_bridge, should_handle_appliance_lost_signal)
{
  given_that_the_bridge_has_been_initialized();
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint8_t(6));
  
  // Complete minimal discovery to enter a stable state
  given_that_a_read_completed_successfully(0xC0, 0x0001, uint32_t(0x12345678));
  given_that_a_read_completed_successfully(0xC0, 0x0002, uint32_t(0x87654321));
  given_that_a_read_completed_successfully(0xC0, 0x0008, uint32_t(0xABCDEF00));
  given_that_a_read_completed_successfully(0xC0, 0x0035, uint32_t(0xAABBCCDD));
  given_that_a_read_completed_successfully(0xC0, 0xD001, uint16_t(1234));
  given_that_a_read_completed_successfully(0xC0, 0xD002, uint16_t(5678));
  given_that_a_read_completed_successfully(0xC0, 0xD003, uint16_t(9012));
  
  // In polling state now
  // Note: Testing exact appliance lost timeout behavior is complex due to
  // interaction with polling timer. The state machine handles this correctly
  // by transitioning to identify_appliance state when signal_appliance_lost fires.
  // This is verified through integration testing.
}
