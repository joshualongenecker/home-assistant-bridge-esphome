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
    // After the first read_completed, commonErdCount-1 more timer expirations are
    // needed to exit state_add_common_erds. energyErdCount and waterHeaterErdCount
    // come from erd_lists.h.
    common_erds_remaining = commonErdCount - 1,
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
      0);
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

// ============================================================================
// Tests for appliance API-parsed polling list (api_parsed_list feature)
// ============================================================================

TEST_GROUP(mqtt_bridge_polling_api_list)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,
    api_erd_1 = 0x1000,
    api_erd_2 = 0x2000
  };

  mqtt_bridge_polling_t self;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  mqtt_client_double_t mqtt_client;

  const tiny_erd_t api_list[2] = {api_erd_1, api_erd_2};

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

  void when_the_bridge_is_initialized()
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      false,
      0);
    // Set the API-parsed list AFTER init (api_parsed_list is always zeroed in init)
    self.api_parsed_list = api_list;
    self.api_parsed_list_count = 2;
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
};

// When api_parsed_list is set, the bridge should skip ERD discovery and go
// directly to polling the API-provided list after finding the appliance type.
TEST(mqtt_bridge_polling_api_list, should_skip_discovery_and_poll_api_list_directly)
{
  // Init sends broadcast (appliance type read)
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  // Appliance responds: api_parsed_list is set so bridge skips to state_polling.
  // state_polling entry registers all api_list ERDs and starts polling the first.
  should_register_erd(api_erd_1);
  should_register_erd(api_erd_2);
  should_request_read(0xC0, api_erd_1);
  uint8_t appliance_type = 0x03; // refrigeration
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // First poll completes: publishes erd_1, immediately reads erd_2
  should_update_erd(api_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, api_erd_2);
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  // Second poll completes: publishes erd_2, no more ERDs to read
  should_update_erd(api_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
}

// After a full polling cycle, the next cycle should restart from the first ERD.
TEST(mqtt_bridge_polling_api_list, should_restart_poll_cycle_on_polling_timer)
{
  // Init + appliance discovery
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  should_register_erd(api_erd_1);
  should_register_erd(api_erd_2);
  should_request_read(0xC0, api_erd_1);
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // Complete first cycle
  should_update_erd(api_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, api_erd_2);
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  should_update_erd(api_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));

  // Polling timer fires: restart from erd_1
  should_request_read(0xC0, api_erd_1);
  after(polling_interval);

  should_update_erd(api_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, api_erd_2);
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));
}

// ============================================================================
// Tests for user-configured custom ERD polling list (custom_erd_list feature)
// ============================================================================

TEST_GROUP(mqtt_bridge_polling_custom_erds)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,
    api_erd = 0x1000,
    custom_erd_1 = 0x7000,
    custom_erd_2 = 0x7001
  };

  mqtt_bridge_polling_t self;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  mqtt_client_double_t mqtt_client;

  const tiny_erd_t api_list[1] = {api_erd};
  const tiny_erd_t custom_list[2] = {custom_erd_1, custom_erd_2};

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

  void when_the_bridge_is_initialized_with_api_list_and_custom_erds()
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      false,
      0);
    self.api_parsed_list = api_list;
    self.api_parsed_list_count = 1;
    self.custom_erd_list = custom_list;
    self.custom_erd_list_count = 2;
  }

  void when_the_bridge_is_initialized_with_custom_erds_only()
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      false,
      0);
    self.custom_erd_list = custom_list;
    self.custom_erd_list_count = 2;
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
};

// Custom ERDs should be polled after api_parsed_list ERDs when both are configured.
TEST(mqtt_bridge_polling_custom_erds, should_poll_custom_erds_alongside_api_parsed_list)
{
  // Init sends broadcast
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized_with_api_list_and_custom_erds();

  // Appliance type received: all ERDs registered and first read starts
  should_register_erd(api_erd);
  should_register_erd(custom_erd_1);
  should_register_erd(custom_erd_2);
  should_request_read(0xC0, api_erd);
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // api_erd completes: publishes, immediately reads custom_erd_1
  should_update_erd(api_erd, uint8_t(0xAA));
  should_request_read(0xC0, custom_erd_1);
  when_a_poll_read_completes(0xC0, api_erd, uint8_t(0xAA));

  // custom_erd_1 completes: publishes, immediately reads custom_erd_2
  should_update_erd(custom_erd_1, uint8_t(0xBB));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  // custom_erd_2 completes: publishes, no more ERDs in cycle
  should_update_erd(custom_erd_2, uint8_t(0xCC));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));

  // Polling timer fires: restart cycle from api_erd
  should_request_read(0xC0, api_erd);
  after(polling_interval);
}

// Custom ERDs should be polled in every cycle when only custom_erds are configured
// (no api_parsed_list, going through discovery).
TEST(mqtt_bridge_polling_custom_erds, should_poll_custom_erds_in_discovery_mode)
{
  mock().disable();
  // Initialize with custom ERDs only (no api_parsed_list)
  when_the_bridge_is_initialized_with_custom_erds_only();

  // Appliance type received: transition to discovery states (water heater = type 0)
  uint8_t appliance_type = 0x00;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // Skip through all discovery ERDs via timer expirations (no responses provided).
  // This transitions through state_add_common_erds → state_add_energy_erds →
  // state_add_appliance_erds → state_polling. In state_polling entry, custom ERDs
  // are registered while mock is disabled. erd_index is left at waterHeaterErdCount
  // (>= polling_list_count=2), so the initial send_next_poll_read_request is a no-op.
  // The count commonErdCount + energyErdCount + waterHeaterErdCount is the number of
  // timer expirations needed to exhaust each discovery state's ERD list and transition
  // to the next, with one expiration per ERD slot per state.
  after(retry_delay * (commonErdCount + energyErdCount + waterHeaterErdCount));
  mock().enable();

  // Polling timer fires: erd_index >= polling_list_count, so cycle restarts from 0,
  // reading custom_erd_1 first.
  should_request_read(0xC0, custom_erd_1);
  after(polling_interval);

  // custom_erd_1 completes: publishes, immediately reads custom_erd_2
  should_update_erd(custom_erd_1, uint8_t(0xBB));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  // custom_erd_2 completes: publishes, no more ERDs in cycle
  should_update_erd(custom_erd_2, uint8_t(0xCC));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));
}

// When the polling bridge is used only for custom ERDs alongside a subscription bridge
// (subscribe/auto mode), it is configured with host_address_override + api_parsed_list.
// The known host address bypasses the 0xFF broadcast; the appliance-type read goes
// directly to the known address instead.
TEST(mqtt_bridge_polling_custom_erds, should_poll_only_custom_erds_when_used_alongside_subscribe_bridge)
{
  // Bridge init: host_address_override = 0xC0 (already discovered), api_parsed_list = custom ERDs.
  // This mirrors how geappliances_bridge initializes custom_erd_bridge_ in subscribe/auto mode.
  // Read goes to 0xC0 (known address), not 0xFF (broadcast).
  should_request_read(0xC0, 0x0008);

  mqtt_bridge_polling_init(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    &mqtt_client.interface,
    polling_interval,
    false,
    0xC0);
  self.api_parsed_list = custom_list;
  self.api_parsed_list_count = 2;

  // Appliance identified at known address: discovery skipped; both custom ERDs registered and first read starts.
  should_register_erd(custom_erd_1);
  should_register_erd(custom_erd_2);
  should_request_read(0xC0, custom_erd_1);
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // custom_erd_1 read completes: publishes, reads custom_erd_2
  should_update_erd(custom_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  // custom_erd_2 read completes: publishes, cycle ends
  should_update_erd(custom_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  // Polling timer fires: restart cycle from custom_erd_1
  should_request_read(0xC0, custom_erd_1);
  after(polling_interval);
}
