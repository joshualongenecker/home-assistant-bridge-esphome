/*!
 * @file
 * @brief Unit tests for the AutodiscoveryManager class.
 *
 * Validates initialization, self-driving state machine transitions,
 * broadcast handling via event subscriptions, retry logic, and
 * completion behavior.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "autodiscovery_manager.h"
#include "esphome_time_source.h"
#include "double/esphome_hal_double.hpp"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_gea2_erd_client_double.hpp"
#include "tiny_timer.h"

#include "geappliances_bridge_constants.h"

using namespace esphome::geappliances_bridge;

/* TEST_GROUP required for TEST(group, name) macro to work. */
TEST_GROUP(autodiscovery_manager)
{
  AutodiscoveryManager manager;
  tiny_gea3_erd_client_double_t gea3_client;
  tiny_gea2_erd_client_double_t gea2_client;
  tiny_gea3_erd_client_double_t gea2_adapter;
  tiny_timer_group_t timer_group;
  bool callback_called;

  void setup()
  {
    mock().strictOrder();
    tiny_gea3_erd_client_double_init(&gea3_client);
    tiny_gea2_erd_client_double_init(&gea2_client);
    tiny_gea3_erd_client_double_init(&gea2_adapter);
    callback_called = false;
    esphome_hal_double_set_millis(0);

    // Initialize a minimal time source for the timer group.
    tiny_timer_group_init(&timer_group, esphome_time_source_init());
  }

  void teardown()
  {
    mock().clear();
  }

  void init_both_uart()
  {
    manager.init(&timer_group,
                 &gea3_client.interface,
                 &gea2_client.interface,
                 &gea2_adapter.interface,
                 nullptr,  /* gea3_interface (not mocked in tests) */
                 nullptr,  /* gea2_interface (not mocked in tests) */
                 true,     /* has_gea3_uart */
                 true,     /* has_gea2_uart */
                 [this]() { callback_called = true; });
  }

  void init_gea3_only()
  {
    manager.init(&timer_group,
                 &gea3_client.interface,
                 nullptr,
                 nullptr,
                 nullptr,  /* gea3_interface (not mocked in tests) */
                 nullptr,  /* gea2_interface (not mocked in tests) */
                 true,     /* has_gea3_uart */
                 false,    /* has_gea2_uart */
                 [this]() { callback_called = true; });
  }

  void init_gea2_only()
  {
    manager.init(&timer_group,
                 nullptr,
                 &gea2_client.interface,
                 &gea2_adapter.interface,  // GEA2 adapter (wraps GEA2 client as GEA3 interface)
                 nullptr,  /* gea3_interface (not mocked in tests) */
                 nullptr,  /* gea2_interface (not mocked in tests) */
                 false,    /* has_gea3_uart */
                 true,     /* has_gea2_uart */
                 [this]() { callback_called = true; });
  }

  /* Expect the GEA3 client to accept a broadcast read. */
  void expect_gea3_broadcast_read()
  {
    mock().expectOneCall("read")
      .onObject(&gea3_client.interface)
      .withParameter("address", GEA_BROADCAST_ADDRESS)
      .withParameter("erd", ERD_APPLIANCE_TYPE)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  /* Expect the GEA2 client to accept a broadcast read. */
  void expect_gea2_broadcast_read()
  {
    mock().expectOneCall("read")
      .onObject(&gea2_client.interface)
      .withParameter("address", GEA_BROADCAST_ADDRESS)
      .withParameter("erd", ERD_APPLIANCE_TYPE)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  /* Simulate a broadcast response by triggering the ERD client activity event. */
  void simulate_broadcast_response(uint8_t address, uint8_t appliance_type, bool is_gea3)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.request_id = 0;
    args.read_completed.erd = ERD_APPLIANCE_TYPE;
    uint8_t data[1] = { appliance_type };
    args.read_completed.data = data;
    args.read_completed.data_size = 1;

    // Use the double's trigger function to publish to the correct client.
    if (is_gea3) {
      tiny_gea3_erd_client_double_trigger_activity_event(&gea3_client, &args);
    } else {
      tiny_gea3_erd_client_double_trigger_activity_event(&gea2_adapter, &args);
    }
  }

  /* Advance timers by running the timer group (services expired timers). */
  void advance_timers()
  {
    // tiny_timer_group_run services at most one timer per call.
    // We call it a few times to drain any pending timers.
    for (int i = 0; i < 10; i++) {
      tiny_timer_group_run(&timer_group);
    }
  }
};

/* ------------------------------------------------------------------ */
/* Initialization                                                       */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, init_sets_state_to_idle)
{
  init_both_uart();
  CHECK_EQUAL(AUTODISCOVERY_IDLE, manager.get_state());
}

TEST(autodiscovery_manager, init_stores_host_address_as_zero)
{
  init_both_uart();
  CHECK_EQUAL(0, manager.get_host_address());
}

TEST(autodiscovery_manager, init_stores_active_erd_client_as_null)
{
  init_both_uart();
  CHECK(manager.get_active_erd_client() == nullptr);
}

TEST(autodiscovery_manager, start_transitions_to_gea3_broadcast_when_both_uart)
{
  init_both_uart();
  expect_gea3_broadcast_read();
  manager.start();
  CHECK_EQUAL(AUTODISCOVERY_GEA3_BROADCAST_WAITING, manager.get_state());
}

TEST(autodiscovery_manager, start_transitions_to_gea2_broadcast_when_gea2_only)
{
  init_gea2_only();
  expect_gea2_broadcast_read();
  manager.start();
  CHECK_EQUAL(AUTODISCOVERY_GEA2_BROADCAST_WAITING, manager.get_state());
}

TEST(autodiscovery_manager, start_is_idempotent)
{
  init_both_uart();
  expect_gea3_broadcast_read();
  manager.start();
  CHECK_EQUAL(AUTODISCOVERY_GEA3_BROADCAST_WAITING, manager.get_state());

  // Second call should be a no-op.
  manager.start();
  CHECK_EQUAL(AUTODISCOVERY_GEA3_BROADCAST_WAITING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* GEA3 discovery success                                               */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, gea3_discovery_completes_on_broadcast_response)
{
  init_both_uart();
  expect_gea3_broadcast_read();
  manager.start();  /* -> GEA3_BROADCAST_WAITING */

  // Simulate a broadcast response arriving via the event subscription.
  simulate_broadcast_response(0xB8, 0x03, true);

  // Advance timers to trigger the broadcast window expiry.
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
  CHECK_EQUAL(0xB8, manager.get_host_address());
  CHECK_EQUAL(&gea3_client.interface, manager.get_active_erd_client());
  CHECK_FALSE(manager.is_gea2_protocol());
  CHECK_TRUE(callback_called);
}

/* ------------------------------------------------------------------ */
/* GEA2 discovery success (after GEA3 timeout)                          */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, gea2_discovery_after_gea3_timeout)
{
  init_both_uart();

  // GEA3 broadcast.
  expect_gea3_broadcast_read();
  manager.start();  /* -> GEA3_BROADCAST_WAITING */

  // GEA3 times out with no response -> retries GEA2.
  expect_gea2_broadcast_read();  // GEA2 broadcast after GEA3 timeout.
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  // Now we should be in GEA2_BROADCAST_WAITING.
  CHECK(manager.get_state() == AUTODISCOVERY_GEA2_BROADCAST_WAITING);

  // Simulate a GEA2 broadcast response.
  simulate_broadcast_response(0xB8, 0x03, false);

  // Advance timers to trigger the broadcast window expiry.
  esphome_hal_double_set_millis(esphome_hal_double_get_millis() + AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
  CHECK_EQUAL(0xB8, manager.get_host_address());
  CHECK_EQUAL(&gea2_adapter.interface, manager.get_active_erd_client());
  CHECK_TRUE(manager.is_gea2_protocol());
  CHECK_TRUE(callback_called);
}

/* ------------------------------------------------------------------ */
/* Wrong protocol response is ignored                                   */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, wrong_protocol_response_ignored)
{
  init_both_uart();
  expect_gea3_broadcast_read();
  manager.start();  /* -> GEA3_BROADCAST_WAITING */

  // A GEA2 response during GEA3 window should be ignored.
  simulate_broadcast_response(0xB8, 0x03, false);

  // The manager should still be waiting (not complete).
  CHECK_EQUAL(AUTODISCOVERY_GEA3_BROADCAST_WAITING, manager.get_state());
  CHECK(manager.get_active_erd_client() == nullptr);
}

/* ------------------------------------------------------------------ */
/* Retry logic: GEA3-only retries GEA3                                  */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, gea3_only_retries_gea3)
{
  init_gea3_only();

  // First GEA3 broadcast.
  expect_gea3_broadcast_read();
  manager.start();  /* -> GEA3_BROADCAST_WAITING */

  // Timeout -> retry GEA3 (timer callback calls run() which sends another read).
  expect_gea3_broadcast_read();  // Second broadcast after retry.
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  // Should be back in GEA3_BROADCAST_WAITING (after sending another read).
  CHECK(manager.get_state() == AUTODISCOVERY_GEA3_BROADCAST_WAITING);
  CHECK(manager.get_active_erd_client() == nullptr);
  CHECK_FALSE(callback_called);
}

/* ------------------------------------------------------------------ */
/* Completion with null callback                                        */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, completion_with_null_callback_does_not_crash)
{
  manager.init(&timer_group,
               &gea3_client.interface,
               &gea2_client.interface,
               &gea2_adapter.interface,
               nullptr,  /* gea3_interface (not mocked) */
               nullptr,  /* gea2_interface (not mocked) */
               true, true,
               nullptr);  /* No callback */

  expect_gea3_broadcast_read();
  manager.start();

  simulate_broadcast_response(0xB8, 0x03, true);
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Complete state is terminal                                           */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, complete_state_is_terminal)
{
  init_both_uart();
  expect_gea3_broadcast_read();
  manager.start();

  simulate_broadcast_response(0xB8, 0x03, true);
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());

  // Further timer advances should not change state.
  esphome_hal_double_set_millis(esphome_hal_double_get_millis() + 100000);
  advance_timers();
  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* is_gea2_protocol returns correct value                               */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, is_gea2_protocol_false_for_gea3_discovery)
{
  init_both_uart();
  expect_gea3_broadcast_read();
  manager.start();

  simulate_broadcast_response(0xB8, 0x03, true);
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  CHECK_FALSE(manager.is_gea2_protocol());
}

TEST(autodiscovery_manager, is_gea2_protocol_true_for_gea2_discovery)
{
  init_both_uart();

  // GEA3 times out.
  expect_gea3_broadcast_read();
  manager.start();
  expect_gea2_broadcast_read();  // GEA2 broadcast after GEA3 timeout.
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  // GEA2 succeeds.
  simulate_broadcast_response(0xB8, 0x03, false);
  esphome_hal_double_set_millis(esphome_hal_double_get_millis() + AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  CHECK_TRUE(manager.is_gea2_protocol());
}
