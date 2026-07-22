/*!
 * @file
 * @brief
 */

#include "erd_bridge_subscribe.h"
#include "erd_registry.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "simulation_test_base.h"

TEST_GROUP_BASE(erd_bridge_subscribe, simulation_test_base)
{
  enum {
    resubscribe_delay = 1000,
    subscription_retention_period = 30 * 1000,
    subscription_quiet_period = 2 * 1000
  };

  erd_bridge_subscribe_t self;

  void setup()
  {
    simulation_test_base_setup();
  }

  void teardown()
  {
    erd_bridge_subscribe_destroy(&self);
    simulation_test_base_teardown();
  }

  void when_the_bridge_is_initialized(uint8_t address = 0xC0)
  {
    erd_bridge_subscribe_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      address,
      &test_cache);
  }

  void given_that_the_bridge_has_been_initialized()
  {
    mock().disable();
    when_the_bridge_is_initialized();
    mock().enable();
  }

  void after_a_subscription_is_added_or_retained_for(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args.address = address;

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  void given_that_a_subscription_has_been_added_or_retained_successfully_for(uint8_t address)
  {
    mock().disable();
    after_a_subscription_is_added_or_retained_for(address);
    mock().enable();
  }

  void given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(uint8_t address)
  {
    given_that_the_bridge_has_been_initialized();
    given_that_a_subscription_has_been_added_or_retained_successfully_for(address);
  }

  void a_subscription_to_should_be_requested_for(uint8_t address)
  {
    mock()
      .expectOneCall("subscribe")
      .onObject(&erd_client)
      .withParameter("address", address)
      .andReturnValue(true);
  }

  void a_subscription_should_be_requested_and_will_fail_to_queue_for(uint8_t address)
  {
    mock()
      .expectOneCall("subscribe")
      .onObject(&erd_client)
      .withParameter("address", address)
      .andReturnValue(false);
  }

  void a_subscription_retention_should_be_requested_for(uint8_t address)
  {
    mock()
      .expectOneCall("retain_subscription")
      .onObject(&erd_client)
      .withParameter("address", address)
      .andReturnValue(true);
  }


  template <typename T>
  void when_an_erd_publication_is_received(uint8_t publisher_address, tiny_erd_t erd, T data)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_publication_received;
    args.address = publisher_address;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data = &data;
    args.subscription_publication_received.data_size = sizeof(data);

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  template <typename T>
  void given_that_an_erd_publication_has_been_received(uint8_t publisher_address, tiny_erd_t erd, T data)
  {
    mock().disable();
    when_an_erd_publication_is_received(publisher_address, erd, data);
    mock().enable();
  }

  void when_a_subscription_host_came_online_is_received_for(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_host_came_online;
    args.address = address;

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  void when_a_subscribe_failure_is_received_for(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscribe_failed;
    args.address = address;

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }
};

TEST(erd_bridge_subscribe, should_subscribe_when_initialized)
{
  a_subscription_to_should_be_requested_for(0xC0);
  when_the_bridge_is_initialized();
}

TEST(erd_bridge_subscribe, should_retry_subscribe_after_a_delay_if_the_subscribe_request_fails_to_queue)
{
  a_subscription_should_be_requested_and_will_fail_to_queue_for(0xC0);
  when_the_bridge_is_initialized();

  after(resubscribe_delay - 1);

  a_subscription_should_be_requested_and_will_fail_to_queue_for(0xC0);
  after(1);

  a_subscription_to_should_be_requested_for(0xC0);
  after(resubscribe_delay);
}

TEST(erd_bridge_subscribe, should_retry_subscribe_if_the_subscribe_request_fails)
{
  given_that_the_bridge_has_been_initialized();
  a_subscription_to_should_be_requested_for(0xC0);
  when_a_subscribe_failure_is_received_for(0xC0);
}

TEST(erd_bridge_subscribe, should_not_retry_subscribe_if_the_subscribe_request_fails_for_a_different_address)
{
  given_that_the_bridge_has_been_initialized();
  when_a_subscribe_failure_is_received_for(0xC1);
}

TEST(erd_bridge_subscribe, should_resubscribe_after_receiving_a_subscription_host_came_online_from_the_erd_host)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);
  a_subscription_to_should_be_requested_for(0xC0);
  when_a_subscription_host_came_online_is_received_for(0xC0);
}

TEST(erd_bridge_subscribe, should_ignore_subscription_host_came_online_from_other_addresses)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);
  when_a_subscription_host_came_online_is_received_for(0xC1);
}

// Regression: the cache should NOT be cleared on host-came-online.
// In AUTO mode the cache is shared with the polling bridge; clearing it
// would destroy the polling bridge's data.
TEST(erd_bridge_subscribe, should_preserve_cache_data_on_host_came_online)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);

  // Populate the cache with an ERD value via a publication.
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));
  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));

  // Simulate the appliance host restarting.
  a_subscription_to_should_be_requested_for(0xC0);
  when_a_subscription_host_came_online_is_received_for(0xC0);

  // The cache entry should still be present (not cleared).
  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));

  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&test_cache, &iter);
  CHECK(entry != nullptr);
  CHECK_EQUAL(0xABCDu, entry->erd);
}

TEST(erd_bridge_subscribe, should_ignore_subscription_added_activity_for_other_addresses)
{
  given_that_the_bridge_has_been_initialized();
  after_a_subscription_is_added_or_retained_for(0xC1);
  after(subscription_retention_period);
}

TEST(erd_bridge_subscribe, should_periodically_retain_an_active_subscription)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);

  // Receive an ERD publication so quiet period transitions to steady (not failed).
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));

  // Advance past the quiet period to reach steady state.
  after(subscription_quiet_period);

  // Now in steady state; retention should fire.
  after(subscription_retention_period - subscription_quiet_period - 1);

  a_subscription_retention_should_be_requested_for(0xC0);
  after(1);
}

TEST(erd_bridge_subscribe, should_register_and_update_newly_discovered_erds_when_published_by_the_erd_client)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);
  when_an_erd_publication_is_received(0xC0, 0xABCD, uint32_t(0x12345678));
}

TEST(erd_bridge_subscribe, should_update_known_erds_when_published_by_the_erd_client)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));
  when_an_erd_publication_is_received(0xC0, 0xABCD, uint32_t(0x87654321));
}

// This makes sure that if we miss the ERD subscription added message that we still handle ERD publications
// Since the ERD client acknowledges publications even if a subscription isn't known to be active, this is
// necessary to make sure that we don't miss any ERD publications
TEST(erd_bridge_subscribe, should_handle_erd_publications_even_when_a_subscription_is_not_confirmed_active)
{
  given_that_the_bridge_has_been_initialized();
  when_an_erd_publication_is_received(0xC0, 0xABCD, uint32_t(0x12345678));
}
TEST(erd_bridge_subscribe, should_ignore_erd_publications_from_other_hosts)
{
  given_that_the_bridge_has_been_initialized();
  when_an_erd_publication_is_received(0xC1, 0xABCD, uint32_t(0x12345678));
}

// Regression: destroy should not crash when erd_client is null.
TEST(erd_bridge_subscribe, should_not_crash_on_destroy_with_null_erd_client)
{
  erd_bridge_subscribe_t unsubscribed;
  memset(&unsubscribed, 0, sizeof(unsubscribed));
  // timer_group is null so the guard returns early; this should not crash.
  erd_bridge_subscribe_destroy(&unsubscribed);
}

// ---------------------------------------------------------------------------
// Steady state tests: quiet period transitions to steady, new ERD exits steady
// ---------------------------------------------------------------------------

TEST(erd_bridge_subscribe, should_transition_to_steady_after_quiet_period_with_no_new_erds)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);

  // Receive at least one ERD publication so the quiet period transitions
  // to steady (not failed — failed is for when no ERDs are published at all).
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));

  // Advance to just before the quiet period — nothing should happen.
  after(subscription_quiet_period - 1);

  // Quiet period elapses, transitioning to steady. No mock expectations
  // needed — the transition is internal.
  after(1);

  // Retention should fire at 30s from when subscribed was entered.
  // We're at 10s, so 20s more to go.
  a_subscription_retention_should_be_requested_for(0xC0);
  after(subscription_retention_period - subscription_quiet_period - 1);

  after(1);
}
TEST(erd_bridge_subscribe, should_return_to_subscribed_on_new_erd_while_steady)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);

  // Receive an ERD publication so quiet period transitions to steady.
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));

  // Advance past the quiet period to reach steady state.
  after(subscription_quiet_period);

  // A new ERD publication should transition back to subscribed.
  when_an_erd_publication_is_received(0xC0, 0x1234, uint32_t(0x42));

  // The quiet timer should have been re-armed; advancing past it again
  // should transition back to steady.
  after(subscription_quiet_period - 1);

  after(1);
}

TEST(erd_bridge_subscribe, should_retain_subscription_in_steady_state)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);

  // Receive an ERD publication so quiet period transitions to steady.
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));

  // Advance past the quiet period to reach steady state.
  after(subscription_quiet_period);

  // Retention should fire at 30s from when subscribed was entered.
  a_subscription_retention_should_be_requested_for(0xC0);
  after(subscription_retention_period - subscription_quiet_period);
}

TEST(erd_bridge_subscribe, should_resubscribe_on_host_came_online_from_steady)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);

  // Receive an ERD publication so quiet period transitions to steady.
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));

  // Advance past the quiet period to reach steady state.
  after(subscription_quiet_period);

  // Host came online from steady should transition to subscribing.
  a_subscription_to_should_be_requested_for(0xC0);
  when_a_subscription_host_came_online_is_received_for(0xC0);
}


// ---------------------------------------------------------------------------
// Dual-subscription tests: two independent bridge instances, each watching a
// different appliance address and publishing to its own MQTT client.
// ---------------------------------------------------------------------------

TEST_GROUP_BASE(erd_bridge_subscribe_dual, simulation_test_base)
{
  enum {
    address_a = 0xC0,
    address_b = 0xC4,
    resubscribe_delay = 1000,
    subscription_retention_period = 30 * 1000
  };

  erd_bridge_subscribe_t bridge_a;
  erd_bridge_subscribe_t bridge_b;

  void setup()
  {
    simulation_test_base_setup();
  }

  void teardown()
  {
    erd_bridge_subscribe_destroy(&bridge_a);
    erd_bridge_subscribe_destroy(&bridge_b);
    simulation_test_base_teardown();
  }

  void given_both_bridges_are_initialized()
  {
    mock().disable();
    erd_bridge_subscribe_init(
      &bridge_a,
      &timer_group.timer_group,
      &erd_client.interface,
      address_a,
      &test_cache);
    erd_bridge_subscribe_init(
      &bridge_b,
      &timer_group.timer_group,
      &erd_client.interface,
      address_b,
      &test_cache);
    mock().enable();
  }

  void a_subscription_should_be_requested_for(uint8_t address)
  {
    mock()
      .expectOneCall("subscribe")
      .onObject(&erd_client)
      .withParameter("address", address)
      .andReturnValue(true);
  }

  void after_a_subscription_is_added_or_retained_for(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args.address = address;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void given_both_subscriptions_are_active()
  {
    mock().disable();
    after_a_subscription_is_added_or_retained_for(address_a);
    after_a_subscription_is_added_or_retained_for(address_b);
    mock().enable();
  }

  template <typename T>
  void when_an_erd_publication_is_received(uint8_t publisher_address, tiny_erd_t erd, T data)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_publication_received;
    args.address = publisher_address;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data = &data;
    args.subscription_publication_received.data_size = sizeof(data);
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

};


TEST(erd_bridge_subscribe_dual, each_bridge_subscribes_to_its_own_address_at_init)
{
  a_subscription_should_be_requested_for(address_a);
  a_subscription_should_be_requested_for(address_b);
  erd_bridge_subscribe_init(
    &bridge_a,
    &timer_group.timer_group,
    &erd_client.interface,
    address_a,
    &test_cache);
  erd_bridge_subscribe_init(
    &bridge_b,
    &timer_group.timer_group,
    &erd_client.interface,
    address_b,
    &test_cache);
}


TEST(erd_bridge_subscribe_dual, each_bridge_independently_retains_its_subscription)
{
  given_both_bridges_are_initialized();
  given_both_subscriptions_are_active();

  // Receive ERD publications so quiet period transitions to steady (not failed).
  when_an_erd_publication_is_received(address_a, 0xABCD, uint32_t(0x12345678));
  when_an_erd_publication_is_received(address_b, 0xABCD, uint32_t(0x12345678));

  // Advance past the quiet period for both bridges.
  after(subscription_quiet_period);

  // Now both are in steady state; retention should fire for both.
  after(subscription_retention_period - subscription_quiet_period - 1);

  mock()
    .expectOneCall("retain_subscription")
    .onObject(&erd_client)
    .withParameter("address", address_a)
    .andReturnValue(true);
  mock()
    .expectOneCall("retain_subscription")
    .onObject(&erd_client)
    .withParameter("address", address_b)
    .andReturnValue(true);
  after(1);
}

TEST(erd_bridge_subscribe_dual, resubscribing_one_bridge_does_not_affect_the_other)
{
  given_both_bridges_are_initialized();
  given_both_subscriptions_are_active();

  // Receive ERD publications so quiet period transitions to steady (not failed).
  when_an_erd_publication_is_received(address_a, 0xABCD, uint32_t(0x12345678));
  when_an_erd_publication_is_received(address_b, 0xABCD, uint32_t(0x12345678));

  // Advance past the quiet period for both bridges.
  after(subscription_quiet_period);

  // bridge_b's host comes back online: only bridge_b should resubscribe
  mock()
    .expectOneCall("subscribe")
    .onObject(&erd_client)
    .withParameter("address", address_b)
    .andReturnValue(true);
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_subscription_host_came_online;
  args.address = address_b;
  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
}

TEST(erd_bridge_subscribe, transitions_to_failed_after_three_consecutive_subscribe_failures)
{
  given_that_the_bridge_has_been_initialized();

  // Each subscribe failure triggers a retry subscribe() call that also fails.
  // After 3 failures, the bridge transitions to state_failed.
  mock().ignoreOtherCalls();

  when_a_subscribe_failure_is_received_for(0xC0);
  when_a_subscribe_failure_is_received_for(0xC0);
  when_a_subscribe_failure_is_received_for(0xC0);

  CHECK(self.current_state == subscription_state_failed);
}

/* ------------------------------------------------------------------ */
/* ERD validity filter                                                */
/* ------------------------------------------------------------------ */

TEST(erd_bridge_subscribe, filter_active_blocks_invalid_erd_from_cache)
{
  esphome::geappliances_bridge::ErdRegistry registry;
  uint16_t valid_erds[] = { 0x0001 };
  registry.set_valid_erds(valid_erds, 1);

  mock().disable();
  erd_bridge_subscribe_init(
    &self, &timer_group.timer_group, &erd_client.interface, 0xC0, &test_cache);
  erd_bridge_subscribe_set_erd_registry(&self, &registry);
  mock().enable();

  // Send publication for ERD 0x0002 (not in valid set).
  uint8_t data = 0xBB;
  mock().ignoreOtherCalls();
  when_an_erd_publication_is_received(0xC0, 0x0002, data);

  // Cache should be empty — invalid ERD was filtered.
  CHECK_EQUAL(0u, erd_cache_get_count(&test_cache));
}

TEST(erd_bridge_subscribe, filter_active_allows_valid_erd_into_cache)
{
  esphome::geappliances_bridge::ErdRegistry registry;
  uint16_t valid_erds[] = { 0x0001 };
  registry.set_valid_erds(valid_erds, 1);

  mock().disable();
  erd_bridge_subscribe_init(
    &self, &timer_group.timer_group, &erd_client.interface, 0xC0, &test_cache);
  erd_bridge_subscribe_set_erd_registry(&self, &registry);
  mock().enable();

  // Send publication for ERD 0x0001 (in valid set).
  uint8_t data = 0xAA;
  mock().ignoreOtherCalls();
  when_an_erd_publication_is_received(0xC0, 0x0001, data);

  // Cache should have the valid ERD.
  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));
  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&test_cache, &iter);
  CHECK(entry != nullptr);
  CHECK_EQUAL(0x0001u, entry->erd);
}

TEST(erd_bridge_subscribe, null_registry_allows_all_erds)
{
  mock().disable();
  erd_bridge_subscribe_init(
    &self, &timer_group.timer_group, &erd_client.interface, 0xC0, &test_cache);
  erd_bridge_subscribe_set_erd_registry(&self, nullptr);
  mock().enable();

  uint8_t data = 0xAA;
  mock().ignoreOtherCalls();
  when_an_erd_publication_is_received(0xC0, 0x0001, data);

  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));
}

TEST(erd_bridge_subscribe, filtered_erd_does_not_affect_hsm_state)
{
  esphome::geappliances_bridge::ErdRegistry registry;
  uint16_t valid_erds[] = { 0x0001 };
  registry.set_valid_erds(valid_erds, 1);

  mock().disable();
  erd_bridge_subscribe_init(
    &self, &timer_group.timer_group, &erd_client.interface, 0xC0, &test_cache);
  erd_bridge_subscribe_set_erd_registry(&self, &registry);
  mock().enable();

  // Trigger subscription added so the bridge enters subscribed state.
  mock().disable();
  after_a_subscription_is_added_or_retained_for(0xC0);
  mock().enable();

  // Send only invalid ERD publications.
  uint8_t data = 0xBB;
  mock().ignoreOtherCalls();
  when_an_erd_publication_is_received(0xC0, 0x0002, data);
  when_an_erd_publication_is_received(0xC0, 0x0003, data);

  // Wait for the quiet period to expire.
  after(subscription_quiet_period + 1);

  // Bridge should transition to failed (no valid ERDs published).
  CHECK(self.current_state == subscription_state_failed);
  // Cache should be empty.
  CHECK_EQUAL(0u, erd_cache_get_count(&test_cache));
}