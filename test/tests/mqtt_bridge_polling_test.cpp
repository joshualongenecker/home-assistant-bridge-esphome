/*!
 * @file
 * @brief Tests for MQTT polling bridge change detection
 */

extern "C" {
#include "mqtt_bridge_polling.h"
}

#include "erd_lists.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/mqtt_client_double.hpp"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"

TEST_GROUP(mqtt_bridge_polling)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,

    // Number of timer expirations needed to skip discovery states.
    // common_erds in mqtt_bridge_polling.cpp has 30 entries; after the first
    // read_completed, 29 more timer expirations are needed to exit that state.
    // energyErdCount and waterHeaterErdCount come from erd_lists.h.
    common_erds_remaining = 29,
    discovery_timer_expirations = common_erds_remaining + energyErdCount + waterHeaterErdCount,

    polled_erd = 0x0001
  };

  mqtt_bridge_polling_t self;

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
    mock().disable();
    mqtt_bridge_polling_destroy(&self);
    mock().enable();
  }

  void when_the_bridge_is_initialized(bool only_publish_on_change = false)
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      only_publish_on_change,
      tiny_gea_broadcast_address);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void trigger_read_completed(uint8_t address, tiny_erd_t erd, const void* data, uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void given_that_the_bridge_has_entered_polling_state(bool only_publish_on_change = false)
  {
    mock().disable();
    when_the_bridge_is_initialized(only_publish_on_change);

    // Identify the appliance (type 0x00 = water heater, 64 ERDs)
    uint8_t appliance_type = 0x00;
    trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

    // Add polled_erd (0x0001) to the polling list via first common ERD read_completed
    uint8_t initial_value = 0x00;
    trigger_read_completed(0xC0, polled_erd, &initial_value, sizeof(initial_value));

    // Skip remaining discovery ERDs using timer expirations
    after(retry_delay * discovery_timer_expirations);

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
  void when_a_poll_read_completes(uint8_t address, tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;
    trigger_read_completed(address, erd, &_value, sizeof(_value));
  }

  void when_the_bridge_is_initialized_with_address(uint8_t address, bool only_publish_on_change = false)
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      only_publish_on_change,
      address);
  }

  void given_that_the_bridge_has_entered_polling_state_with_address(
    uint8_t address, bool only_publish_on_change = false)
  {
    mock().disable();
    when_the_bridge_is_initialized_with_address(address, only_publish_on_change);

    // No broadcast identification needed: bridge starts directly in state_add_common_erds
    uint8_t initial_value = 0x00;
    trigger_read_completed(address, polled_erd, &initial_value, sizeof(initial_value));

    // Skip remaining discovery ERDs using timer expirations
    after(retry_delay * discovery_timer_expirations);

    mock().enable();
  }

  void nothing_should_happen()
  {
  }
};

TEST(mqtt_bridge_polling, should_always_publish_mqtt_when_only_publish_on_change_is_disabled)
{
  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(mqtt_bridge_polling, should_publish_mqtt_on_first_poll_when_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(mqtt_bridge_polling, should_not_republish_mqtt_when_polled_erd_data_is_unchanged_and_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(mqtt_bridge_polling, should_republish_mqtt_when_polled_erd_data_changes_and_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x02));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x02));
}

// A late response from a discovery-phase read that arrives after the state
// machine has already transitioned to polling (device responded slower than
// retry_delay). The ERD must be registered and added to the polling list.
TEST(mqtt_bridge_polling, should_register_and_poll_erd_whose_discovery_response_arrives_late_in_polling_state)
{
  enum { late_erd = 0x7b00 };

  given_that_the_bridge_has_entered_polling_state();

  // Cycle 1: polling timer fires and begins reading polled_erd
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // Late discovery response for late_erd arrives before polled_erd responds.
  // Bridge registers it, publishes its value, then immediately reads the
  // newly-registered ERD (now at the next position in the polling list).
  should_register_erd(late_erd);
  should_update_erd(late_erd, uint8_t(0xAB));
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));

  // polled_erd arrives next; erd_index is now at the end so no further read
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  // Cycle 2: late_erd is now in the polling list alongside polled_erd
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // polled_erd completes and immediately triggers the read for late_erd
  should_update_erd(polled_erd, uint8_t(0x01));
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_update_erd(late_erd, uint8_t(0xAB));
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));
}

// Same late-response scenario with only_publish_on_change enabled.
TEST(mqtt_bridge_polling, should_register_and_poll_late_erd_when_only_publish_on_change_is_enabled)
{
  enum { late_erd = 0x7b05 };

  given_that_the_bridge_has_entered_polling_state(true);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // New ERD: always published on first read; bridge immediately reads the
  // newly-registered ERD (appended to the polling list).
  should_register_erd(late_erd);
  should_update_erd(late_erd, uint8_t(0xCD));
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));

  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  // Cycle 2: both ERDs polled; values unchanged → neither is republished
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // polled_erd same value → not published; late_erd read is triggered inline
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  nothing_should_happen();
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));
}

// When initialized with a specific (non-broadcast) address, the bridge skips
// state_identify_appliance and polls that address directly.  This is the
// mechanism that allows a second board (e.g. 0xD0) to be bridged alongside
// the primary board (0xC0) using two separate bridge instances.
TEST(mqtt_bridge_polling, should_poll_preset_address_without_broadcast_discovery)
{
  enum { second_board_address = 0xD0 };

  given_that_the_bridge_has_entered_polling_state_with_address(second_board_address);

  should_request_read(second_board_address, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x42));
  when_a_poll_read_completes(second_board_address, polled_erd, uint8_t(0x42));
}
