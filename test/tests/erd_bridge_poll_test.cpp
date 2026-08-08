/*!
 * @file
 * @brief Tests for ERD polling bridge with pre-built probe list.
 *
 * The polling bridge now receives a pre-built probe list from the
 * erd_poll_list_builder module. Tests drive the HSM through:
 *   state_probe_list → state_polling
 */

#include "erd_bridge_poll.h"
#include "erd_bridge_common.h"

#include "erd_lists.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "simulation_test_base.h"

TEST_GROUP_BASE(erd_bridge_poll, simulation_test_base)
{
  enum {
    polling_interval = 1000,
    polled_erd = 0x0001
  };

  erd_bridge_poll_t self;

  void setup()
  {
    simulation_test_base_setup();
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&self);
    simulation_test_base_teardown();
    mock().enable();
  }

  void when_the_bridge_is_initialized_with_probe_list(const probe_entry_t* list, uint16_t count)
  {
    erd_bridge_poll_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xC0, list, count,
      &test_cache);
  }

  void given_that_the_bridge_has_entered_polling_state()
  {
    mock().disable();
    const probe_entry_t probe_list[] = {{polled_erd, PROBE_ENTRY_DEFAULT_ADDRESS}};
    when_the_bridge_is_initialized_with_probe_list(probe_list, 1);

    // Probe phase: the single ERD responds successfully.
    uint8_t initial_value = 0x00;
    trigger_read_completed(0xC0, polled_erd, &initial_value, sizeof(initial_value));

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


  template <typename T>
  void when_a_poll_read_completes(uint8_t address, tiny_erd_t erd, T value)
  {
    trigger_read_completed(address, erd, &value, sizeof(value));
  }

};

// Regression: the cache should NOT be cleared on re-probe after appliance lost.
// In AUTO mode the cache is shared with the subscription bridge; clearing it
// would destroy the subscription bridge's data.

TEST(erd_bridge_poll, should_preserve_cache_data_on_reprobe_after_appliance_lost)
{
  mock().disable();
  const probe_entry_t probe_list[] = {{polled_erd, PROBE_ENTRY_DEFAULT_ADDRESS}};
  when_the_bridge_is_initialized_with_probe_list(probe_list, 1);

  // Probe phase: ERD responds, bridge enters polling.
  uint8_t initial_value = 0x42;
  trigger_read_completed(0xC0, polled_erd, &initial_value, sizeof(initial_value));
  mock().enable();

  // Bridge should now be in polling state with 1 ERD in the cache.
  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));

  // Set up mock for re-probe after appliance lost.
  should_request_read(0xC0, polled_erd);

  // Trigger appliance lost signal.
  tiny_hsm_send_signal(&self.hsm, signal_appliance_lost, nullptr);

  // Re-probe: the ERD responds with a new value.
  uint8_t new_value = 0x99;
  trigger_read_completed(0xC0, polled_erd, &new_value, sizeof(new_value));

  // The cache should still have 1 entry with the updated value.
  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));

  // Verify the ERD has the updated value.
  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&test_cache, &iter);
  CHECK(entry != nullptr);
  CHECK_EQUAL(polled_erd, entry->erd);
  CHECK_EQUAL(sizeof(new_value), entry->data_size);
}

TEST(erd_bridge_poll, keeps_same_erd_from_different_board_addresses_separate)
{
  mock().disable();
  const probe_entry_t probe_list[] = {
    {polled_erd, 0xC0},
    {polled_erd, 0xA2},
  };
  when_the_bridge_is_initialized_with_probe_list(probe_list, 2);

  uint8_t primary_value = 0x11;
  uint8_t inverter_value = 0x22;
  trigger_read_completed(0xC0, polled_erd, &primary_value, sizeof(primary_value));
  trigger_read_completed(0xA2, polled_erd, &inverter_value, sizeof(inverter_value));
  mock().enable();

  CHECK_EQUAL(2u, erd_cache_get_count(&test_cache));
  erd_cache_entry_t* primary = nullptr;
  erd_cache_entry_t* inverter = nullptr;
  uint16_t iterator = 0;
  while (erd_cache_entry_t* entry = erd_cache_get_next_entry(&test_cache, &iterator)) {
    if (entry->erd == polled_erd && entry->board_address == PROBE_ENTRY_DEFAULT_ADDRESS) primary = entry;
    if (entry->erd == polled_erd && entry->board_address == 0xA2) inverter = entry;
  }
  CHECK(primary != nullptr);
  CHECK(inverter != nullptr);
  CHECK_EQUAL(0x11, *erd_cache_entry_data(&test_cache, primary));
  CHECK_EQUAL(0x22, *erd_cache_entry_data(&test_cache, inverter));
}

TEST(erd_bridge_poll, should_publish_mqtt_on_first_poll)
{
  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}
TEST(erd_bridge_poll, should_not_republish_mqtt_when_polled_erd_data_is_unchanged)
{
  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}
TEST(erd_bridge_poll, should_republish_mqtt_when_polled_erd_data_changes)
{
  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x02));
}

// A late response from a discovery-phase read that arrives after the state
// machine has already transitioned to polling (device responded slower than
// retry_delay). The ERD must be registered and added to the polling list.
TEST(erd_bridge_poll, should_register_and_poll_erd_whose_discovery_response_arrives_late_in_polling_state)
{
  enum { late_erd = 0x7b00 };

  given_that_the_bridge_has_entered_polling_state();

  // Cycle 1: polling timer fires and begins reading polled_erd
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // Late discovery response for late_erd arrives before polled_erd responds.
  // Bridge registers it and publishes its value. With simultaneous reads,
  // the late ERD is added to the polling list but won't be read until
  // the next cycle (all reads for this cycle were already fired).
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));

  // polled_erd arrives next
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  // Cycle 2: late_erd is now in the polling list alongside polled_erd,
  // both are read simultaneously
  should_request_read(0xC0, polled_erd);
  should_request_read(0xC0, late_erd);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));
}

// Same late-response scenario, default on-change behavior.
TEST(erd_bridge_poll, should_register_and_poll_late_erd)
{
  enum { late_erd = 0x7b05 };

  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // New ERD: always published on first read. With simultaneous reads,
  // the late ERD is added to the polling list but won't be read until
  // the next cycle.
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));

  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  // Cycle 2: both ERDs polled simultaneously; values unchanged → neither republished
  should_request_read(0xC0, polled_erd);
  should_request_read(0xC0, late_erd);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));
}

// ============================================================================
// Tests for probe list polling (pre-built list from erd_poll_list_builder)
// ============================================================================

TEST_GROUP_BASE(erd_bridge_poll_probe_list, simulation_test_base)
{
  enum {
    polling_interval = 1000,
    probe_erd_1 = 0x1000,
    probe_erd_2 = 0x2000
  };

  erd_bridge_poll_t self;

  const probe_entry_t probe_list[2] = {{probe_erd_1, PROBE_ENTRY_DEFAULT_ADDRESS}, {probe_erd_2, PROBE_ENTRY_DEFAULT_ADDRESS}};

  void setup()
  {
    simulation_test_base_setup();
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&self);
    simulation_test_base_teardown();
    mock().enable();
  }

  void when_the_bridge_is_initialized_with_probe_list()
  {
    erd_bridge_poll_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xC0, probe_list, 2,
      &test_cache);
  }

  void trigger_read_failed_not_supported(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_not_supported;
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


  template <typename T>
  void when_a_poll_read_completes(uint8_t address, tiny_erd_t erd, T value)
  {
    trigger_read_completed(address, erd, &value, sizeof(value));
  }

};

// When probe list is provided with a known host, the bridge skips broadcast
// and goes straight to probing the list.
TEST(erd_bridge_poll_probe_list, should_probe_list_and_poll_successfully)
{
  mock().disable();
  when_the_bridge_is_initialized_with_probe_list();

  // Probe phase: both ERDs respond successfully.
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, probe_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, probe_erd_2, &probe_val, sizeof(probe_val));
  mock().enable();

  // Polling timer fires: both ERDs read
  should_request_read(0xC0, probe_erd_1);
  should_request_read(0xC0, probe_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, probe_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, probe_erd_2, uint8_t(0xBB));
}

// An ERD that the appliance rejects with "not_supported" during probe
// is excluded from the polling list.
TEST(erd_bridge_poll_probe_list, should_exclude_failed_erd_from_polling_list)
{
  mock().disable();
  when_the_bridge_is_initialized_with_probe_list();

  // Probe phase: first ERD succeeds, second fails.
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, probe_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_failed_not_supported(probe_erd_2);
  mock().enable();

  // Polling timer fires: only the successful ERD is polled.
  should_request_read(0xC0, probe_erd_1);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, probe_erd_1, uint8_t(0xAA));
}

// When all ERDs in the probe list fail, the bridge enters polling with an
// empty list.
TEST(erd_bridge_poll_probe_list, should_handle_all_probe_failures)
{
  mock().disable();
  when_the_bridge_is_initialized_with_probe_list();

  // Probe phase: both ERDs fail.
  trigger_read_failed(probe_erd_1);
  trigger_read_failed(probe_erd_2);
  mock().enable();

  // Bridge is in polling state with empty list.
  // Timer fires but there's nothing to poll.
  after(polling_interval);
}

// Polling timer restarts the cycle.
TEST(erd_bridge_poll_probe_list, should_restart_poll_cycle_on_polling_timer)
{
  mock().disable();
  when_the_bridge_is_initialized_with_probe_list();

  // Probe phase: both ERDs respond.
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, probe_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, probe_erd_2, &probe_val, sizeof(probe_val));
  mock().enable();

  // First poll cycle
  should_request_read(0xC0, probe_erd_1);
  should_request_read(0xC0, probe_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, probe_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, probe_erd_2, uint8_t(0xBB));

  // Second poll cycle
  should_request_read(0xC0, probe_erd_1);
  should_request_read(0xC0, probe_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, probe_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, probe_erd_2, uint8_t(0xBB));
}

// ============================================================================
// Tests for empty probe list
// ============================================================================

TEST_GROUP_BASE(erd_bridge_poll_empty_list, simulation_test_base)
{
  enum { polling_interval = 1000 };

  erd_bridge_poll_t self;

  void setup()
  {
    simulation_test_base_setup();
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&self);
    simulation_test_base_teardown();
    mock().enable();
  }

};

// When probe list is empty, the bridge transitions directly to polling
// without sending any read requests.
TEST(erd_bridge_poll_empty_list, should_skip_probe_and_enter_polling_with_empty_list)
{
  mock().disable();
  erd_bridge_poll_init(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    polling_interval,
    0xC0, nullptr, 0,
    &test_cache);

  // Bridge should be in polling state with empty list.
  // No ERDs in cache.
  CHECK_EQUAL(0u, erd_cache_get_count(&test_cache));
  mock().enable();
}

// ============================================================================
// Tests for probe list with retries_exhausted failures
// ============================================================================

TEST_GROUP_BASE(erd_bridge_poll_probe_failures, simulation_test_base)
{
  enum {
    polling_interval = 1000,
    probe_erd_1 = 0x1000,
    probe_erd_2 = 0x2000,
    probe_erd_3 = 0x3000
  };

  erd_bridge_poll_t self;

  const probe_entry_t probe_list[3] = {{probe_erd_1, PROBE_ENTRY_DEFAULT_ADDRESS}, {probe_erd_2, PROBE_ENTRY_DEFAULT_ADDRESS}, {probe_erd_3, PROBE_ENTRY_DEFAULT_ADDRESS}};

  void setup()
  {
    simulation_test_base_setup();
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&self);
    simulation_test_base_teardown();
    mock().enable();
  }

  void when_the_bridge_is_initialized_with_probe_list()
  {
    erd_bridge_poll_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xC0, probe_list, 3,
      &test_cache);
  }

  void trigger_read_failed_retries_exhausted(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_retries_exhausted;
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


  template <typename T>
  void when_a_poll_read_completes(uint8_t address, tiny_erd_t erd, T value)
  {
    trigger_read_completed(address, erd, &value, sizeof(value));
  }

};

// retries_exhausted during probe excludes the ERD from polling (same as not_supported).
TEST(erd_bridge_poll_probe_failures, should_exclude_retries_exhausted_erd_from_polling_list)
{
  mock().disable();
  when_the_bridge_is_initialized_with_probe_list();

  // Probe phase: first two ERDs succeed, third fails with retries_exhausted.
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, probe_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, probe_erd_2, &probe_val, sizeof(probe_val));
  trigger_read_failed_retries_exhausted(probe_erd_3);
  mock().enable();

  // Polling timer fires: only the successful ERDs are polled.
  should_request_read(0xC0, probe_erd_1);
  should_request_read(0xC0, probe_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, probe_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, probe_erd_2, uint8_t(0xBB));
}

// Regression: destroy should not crash when erd_client is null.
// This can happen if init was called with a null erd_client pointer.
TEST(erd_bridge_poll, should_not_crash_on_destroy_with_null_erd_client)
{
  self.timer_group = &timer_group.timer_group;
  self.erd_client = NULL;

  erd_bridge_poll_destroy(&self);
}
