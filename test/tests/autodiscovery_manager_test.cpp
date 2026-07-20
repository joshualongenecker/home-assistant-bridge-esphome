/*!
 * @file
 * @brief Unit tests for the AutodiscoveryManager class.
 *
 * Validates initialization, self-driving state machine transitions,
 * broadcast handling via byte-level UART subscription, retry logic, and
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
#include "tiny_crc16.h"
#include "tiny_gea_constants.h"
#include "tiny_gea_packet.h"
#include "tiny_gea3_erd_api.h"
#include "tiny_gea2_erd_api.h"

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
                 nullptr,  /* gea3_uart_adapter (not mocked in tests) */
                 nullptr,  /* gea2_uart_adapter (not mocked in tests) */
                 true,     /* has_gea3_uart */
                 true,     /* has_gea2_uart */
                 0xE4,     /* client_address */
                 [this]() { callback_called = true; });
  }

  void init_gea3_only()
  {
    manager.init(&timer_group,
                 &gea3_client.interface,
                 nullptr,
                 nullptr,
                 nullptr,  /* gea3_uart_adapter (not mocked in tests) */
                 nullptr,  /* gea2_uart_adapter (not mocked in tests) */
                 true,     /* has_gea3_uart */
                 false,    /* has_gea2_uart */
                 0xE4,     /* client_address */
                 [this]() { callback_called = true; });
  }

  void init_gea2_only()
  {
    manager.init(&timer_group,
                 nullptr,
                 &gea2_client.interface,
                 &gea2_adapter.interface,  // GEA2 adapter (wraps GEA2 client as GEA3 interface)
                 nullptr,  /* gea3_uart_adapter (not mocked in tests) */
                 nullptr,  /* gea2_uart_adapter (not mocked in tests) */
                 false,    /* has_gea3_uart */
                 true,     /* has_gea2_uart */
                 0xE4,     /* client_address */
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

  /*
   * Simulate a broadcast response by feeding raw GEA packet bytes
   * through the manager's byte-level packet assembler.
   *
   * Constructs a valid GEA frame:
   *   STX | destination | payload_length | source | payload | CRC(2) | ETX
   *
   * For GEA3: payload = A1 cmd | request_id | result | erd(2) | data_size | data
   * For GEA2: payload = F0 cmd | erd_count | erd(2) | data_size | data
   */
  void simulate_broadcast_response(uint8_t address, uint8_t appliance_type, bool is_gea3, uint8_t destination = 0xE4)
  {
    // Build the application payload
    uint8_t payload[16];
    int payload_len;

    if (is_gea3) {
      // GEA3 read response: cmd | request_id | result | erd(2) | data_size | data
      payload[0] = tiny_gea3_erd_api_command_read_response;
      payload[1] = 0;  // request_id
      payload[2] = tiny_gea3_erd_api_read_result_success;
      payload[3] = (ERD_APPLIANCE_TYPE >> 8) & 0xFF;
      payload[4] = ERD_APPLIANCE_TYPE & 0xFF;
      payload[5] = 1;  // data_size
      payload[6] = appliance_type;
      payload_len = 7;
    } else {
      // GEA2 read response: cmd | erd_count | erd(2) | data_size | data
      payload[0] = tiny_gea2_erd_api_command_read_response;
      payload[1] = 1;  // erd_count
      payload[2] = (ERD_APPLIANCE_TYPE >> 8) & 0xFF;
      payload[3] = ERD_APPLIANCE_TYPE & 0xFF;
      payload[4] = 1;  // data_size
      payload[5] = appliance_type;
      payload_len = 6;
    }

    // Build the GEA frame (without STX/ETX/CRC)
    // destination | payload_length_on_wire | source | payload
    uint8_t frame[256];
    int frame_idx = 0;
    frame[frame_idx++] = destination;  // destination (bridge address)
    int payload_length_on_wire = tiny_gea_packet_transmission_overhead + payload_len;
    frame[frame_idx++] = payload_length_on_wire;
    frame[frame_idx++] = address;  // source (board address)
    for (int i = 0; i < payload_len; i++) {
      frame[frame_idx++] = payload[i];
    }
    int frame_len = frame_idx;

    // Compute CRC over the frame bytes, then include CRC bytes themselves
    // so that the running CRC ends at 0 (matching receiver behavior).
    uint16_t crc = tiny_gea_crc_seed;
    for (int i = 0; i < frame_len; i++) {
      crc = tiny_crc16_byte(crc, frame[i]);
    }

    // Feed bytes through the manager: STX, frame bytes, CRC(2), ETX
    // CRC is transmitted MSB first, then LSB (matching GEA protocol).
    manager.feed_byte(tiny_gea_stx, is_gea3);
    for (int i = 0; i < frame_len; i++) {
      manager.feed_byte(frame[i], is_gea3);
    }
    manager.feed_byte((crc >> 8) & 0xFF, is_gea3); // CRC MSB
    manager.feed_byte(crc & 0xFF, is_gea3);       // CRC LSB
    manager.feed_byte(tiny_gea_etx, is_gea3);
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

  // Calling start() again should be a no-op (state is not IDLE).
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

  // Simulate a broadcast response arriving via byte-level UART.
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
               nullptr,  /* gea3_uart_adapter (not mocked) */
               nullptr,  /* gea2_uart_adapter (not mocked) */
               true, true,
               0xE4,     /* client_address */
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

/* ------------------------------------------------------------------ */
/* Non-default client address regression tests                         */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, non_default_client_address_accepts_response)
{
  // Regression test: when adapter_address is configured to a non-default
  // value (e.g. 0xE5), discovery responses addressed to that value must
  // be accepted. Previously, the destination filter hardcoded 0xE4.
  manager.init(&timer_group,
               &gea3_client.interface,
               &gea2_client.interface,
               &gea2_adapter.interface,
               nullptr, nullptr,
               true, true,
               0xE5,     /* client_address — non-default */
               [this]() { callback_called = true; });

  expect_gea3_broadcast_read();
  manager.start();

  // Response addressed to 0xE5 (our configured address) — must be accepted.
  simulate_broadcast_response(0xB8, 0x03, true, 0xE5);
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
  CHECK_EQUAL(0xB8, manager.get_host_address());
  CHECK_TRUE(callback_called);
}

TEST(autodiscovery_manager, non_default_client_address_rejects_wrong_destination)
{
  // Negative test: a response addressed to a different client address
  // must be silently ignored.
  manager.init(&timer_group,
               &gea3_client.interface,
               &gea2_client.interface,
               &gea2_adapter.interface,
               nullptr, nullptr,
               true, true,
               0xE5,     /* client_address */
               [this]() { callback_called = true; });

  expect_gea3_broadcast_read();
  manager.start();
  expect_gea2_broadcast_read();  // GEA2 fallback after GEA3 timeout

  // Response addressed to 0xE4 (not us) — must be ignored.
  simulate_broadcast_response(0xB8, 0x03, true, 0xE4);
  esphome_hal_double_set_millis(AUTODISCOVERY_BROADCAST_WINDOW_MS + 100);
  advance_timers();

  // Discovery should have timed out (no valid response received).
  CHECK(manager.get_state() != AUTODISCOVERY_COMPLETE);
  CHECK_FALSE(callback_called);
}
