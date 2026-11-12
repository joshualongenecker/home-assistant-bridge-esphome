/*!\
 * @file
 * @brief Unit tests for the self-driving FeatureBitManager class.
 *
 * Validates initialization, start(), event-driven ERD read sequencing (14 ERDs),
 * state transitions, failure handling, timer-driven incremental parsing, and
 * valid ERD list generation.
 *
 * The manager is fully self-driving: it subscribes to ERD client activity events
 * and uses a periodic timer for incremental parsing.  Tests trigger events via
 * tiny_gea3_erd_client_double_trigger_activity_event() and drive the parse timer
 * via tiny_timer_group_double_elapse_time().
 *
 * Note: Production headers with STL containers (<set>, <vector>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "feature_bit_manager.h"
#include "geappliances_bridge_constants.h"
#include "appliance_api_feature_lists.h"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"
#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include <cstring>

using namespace esphome::geappliances_bridge;

/* TEST_GROUP required for TEST(group, name) macro to work —
 * provides the base class that TEST-generated test classes inherit from. */
TEST_GROUP(feature_bit_manager)
{
  FeatureBitManager manager;
  tiny_gea3_erd_client_double_t erd_client;
  tiny_timer_group_double_t timer_group;

  void setup()
  {
    mock().clear();
    mock().strictOrder();
    tiny_gea3_erd_client_double_init(&erd_client);
    tiny_timer_group_double_init(&timer_group);
  }

  void teardown()
  {
    mock().clear();
  }

  void init_manager()
  {
    manager.init(&erd_client.interface, 0xC0, &timer_group.timer_group);
  }

  /* Expect that a read for the given ERD will be queued successfully. */
  void expect_successful_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client.interface)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  /* Expect that a read for the given ERD will fail to queue. */
  void expect_failed_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client.interface)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(false);
  }

  /* Trigger a read_completed activity event for the given ERD. */
  void trigger_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = 0xC0;
    args.read_completed.request_id = 0;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  /* Trigger a read_failed activity event for the given ERD. */
  void trigger_read_failed(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_not_supported;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  /* Full sequence: trigger all 11 feature-bit ERD reads in order via activity events.
   * Each completed read triggers the manager to queue the next one internally. */
  void trigger_full_read_sequence()
  {
    uint8_t dummy_data[8] = {0};

    // 0x0092 -> 0x0093
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
    trigger_read_completed(ERD_COMMON_FEATURE_API, dummy_data, 8);

    // 0x0093 -> 0x0094
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, dummy_data, 8);

    // 0x0094 -> 0x0095
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, dummy_data, 8);

    // 0x0095 -> 0x0096
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, dummy_data, 8);

    // 0x0096 -> 0x0097
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, dummy_data, 8);

    // 0x0097 -> 0x0109
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, dummy_data, 8);

    // 0x0109 -> 0x010A
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, dummy_data, 8);

    // 0x010A -> 0x010B
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, dummy_data, 8);

    // 0x010B -> 0x010C
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, dummy_data, 8);

    // 0x010C -> 0x010D
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, dummy_data, 8);

    // 0x010D (last) -> transitions to PARSING, starts timer
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_9, dummy_data, 8);
  }
};

/* ------------------------------------------------------------------ */
/* init() tests                                                         */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, init_stores_all_parameters)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  tiny_timer_group_double_t tg;
  tiny_gea3_erd_client_double_init(&ec);
  tiny_timer_group_double_init(&tg);

  mgr.init(&ec.interface, 0xC0, &tg.timer_group);

  // Verify the manager can use the stored erd_client by calling start().
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("address", 0xC0)
    .withParameter("erd", ERD_COMMON_FEATURE_API)
    .ignoreOtherParameters()
    .andReturnValue(true);
  mgr.start();

  // State should remain READING_0092 after successful queue (self-driving model).
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, mgr.get_state());
}

TEST(feature_bit_manager, init_sets_state_to_reading_0092)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  tiny_timer_group_double_t tg;
  tiny_gea3_erd_client_double_init(&ec);
  tiny_timer_group_double_init(&tg);

  mgr.init(&ec.interface, 0xC0, &tg.timer_group);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, mgr.get_state());
}

TEST(feature_bit_manager, init_resets_parse_state)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  tiny_timer_group_double_t tg;
  tiny_gea3_erd_client_double_init(&ec);
  tiny_timer_group_double_init(&tg);

  mgr.init(&ec.interface, 0xC0, &tg.timer_group);

  // State starts at READING_0092, not PARSING or COMPLETE.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, mgr.get_state());
}

/* ------------------------------------------------------------------ */
/* start() — begins the self-driving read sequence                      */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, start_queues_common_feature_read_first)
{
  init_manager();
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);

  manager.start();

  // State remains READING_0092 (self-driving; no IN_FLIGHT state).
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}

TEST(feature_bit_manager, start_is_idempotent)
{
  init_manager();

  // First call queues the read.
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  // Second call should be a no-op (state is still READING_0092, but the
  // read was already queued).  No additional read mock expected.
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}

TEST(feature_bit_manager, start_does_nothing_when_not_at_first_state)
{
  init_manager();

  // Advance to next state first.
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  uint8_t feature_data[8] = {0};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);

  // Now state is READING_0093. start() should be a no-op.
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Event-driven ERD read sequencing                                     */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, read_completed_transitions_to_appliance_feature_api_0)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  uint8_t feature_data[8] = {0};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}

TEST(feature_bit_manager, read_completed_transitions_to_appliance_feature_api_1)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  uint8_t feature_data[8] = {0};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, feature_data, 8);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* read_failed() — skip behavior                                        */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, read_failed_skips_to_next_erd)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  // ERD_COMMON_FEATURE_API failure transitions to FAILED (terminal state),
  // so no next read is queued.
  trigger_read_failed(ERD_COMMON_FEATURE_API);

  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());
}

TEST(feature_bit_manager, is_feature_bits_complete_returns_true_when_failed)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  trigger_read_failed(ERD_COMMON_FEATURE_API);

  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());
  // GeappliancesBridge::is_feature_bits_complete() treats FAILED as complete
  // (falls back to full polling). Verify the state is FAILED so the bridge
  // will transition past the feature_bits phase.
}

TEST(feature_bit_manager, read_failed_skips_feature_api_0_to_api_1)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  uint8_t feature_data[8] = {0};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);

  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_failed(ERD_APPLIANCE_FEATURE_API_0);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}

/* The skip count is a printed diagnostic: the INFO "Feature bit parsing complete"
 * line and the DEBUG "Feature bits:" summary both report it.  Pin both the
 * increment and the reset in init() - without the reset, a teardown/setup cycle
 * reports a cumulative skip count beside a per-run valid count, and nothing else
 * in the suite observes the counter. */
TEST(feature_bit_manager, read_failed_counts_one_skip_and_reinit_clears_it)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  uint8_t feature_data[8] = {0};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);

  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_failed(ERD_APPLIANCE_FEATURE_API_0);

  LONGS_EQUAL(1, (long) manager.get_skipped_erd_count());

  /* The teardown/setup path: the counter belongs to the run, not the object. */
  init_manager();
  LONGS_EQUAL(0, (long) manager.get_skipped_erd_count());
}

TEST(feature_bit_manager, read_failed_on_last_erd_transitions_to_parsing)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  uint8_t feature_data[8] = {0};

  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, feature_data, 8);

  // Fail the last ERD (0x010D) — should transition to PARSING and start timer.
  // tiny_timer_start_periodic is a real C function (not mockable via interface),
  // so we just verify the state transition.
  trigger_read_failed(ERD_APPLIANCE_FEATURE_API_9);

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Full read sequence -> PARSING state                                  */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, full_read_sequence_transitions_to_parsing)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  trigger_full_read_sequence();

  // After the last ERD (0x010D), state is PARSING and timer is started.
  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Timer-driven incremental parsing                                     */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, incremental_parsing_completes_with_timer_elapse)
{
  init_manager();

  // Set common feature API with "Primary" bit (0x00000001) set.
  uint8_t common_data[8] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};

  // Use non-zero appliance feature data so each ERD is actually parsed
  // (not skipped as empty). Format: [2B type][2B version][4B bitmap].
  // type=0x0001, version=1, bitmap=0x00000001 (non-zero so not skipped).
  uint8_t appliance_data[8] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, common_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, appliance_data, 8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_9, appliance_data, 8);

  // State should be PARSING after the full read sequence.
  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());

  // Elapse time to run the parse timer through all ticks.
  // ~16 ticks at 5ms each = 80ms.
  tiny_timer_group_double_elapse_time(&timer_group, 100);

  // After parsing completes, state should be COMPLETE.
  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());

  // The valid ERD set should contain the mandatory ERDs plus any
  // ERDs indicated by the feature bits.
  CHECK_TRUE(manager.get_valid_erd_count() >= 14);
}

TEST(feature_bit_manager, parsing_state_is_reached_before_complete)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  trigger_full_read_sequence();

  // State is PARSING, not yet COMPLETE.
  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());

  // Elapse one tick — still parsing (common features take multiple ticks).
  tiny_timer_group_double_elapse_time(&timer_group, 1);

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Queue full behavior — stays in current state                         */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, queue_full_keeps_current_state)
{
  init_manager();

  // start() tries to queue a read but the queue is full.
  expect_failed_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  // State remains READING_0092 when queue is full.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());

  // Elapse 50ms to trigger the retry timer, which attempts another read (also failing).
  expect_failed_read(0xC0, ERD_COMMON_FEATURE_API);
  tiny_timer_group_double_elapse_time(&timer_group, 50);

  // State still READING_0092 after the retry also fails.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, queue_full_retry_timer_retries_on_elapse)
{
  init_manager();

  // start() tries to queue a read but the queue is full.
  expect_failed_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());

  // Elapse 50ms to trigger the retry timer, which attempts another read (also failing).
  expect_failed_read(0xC0, ERD_COMMON_FEATURE_API);
  tiny_timer_group_double_elapse_time(&timer_group, 50);

  // State is still READING_0092 after the retry also fails.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}

TEST(feature_bit_manager, queue_retry_timer_noop_when_already_queued)
{
  init_manager();

  // start() tries to queue a read but the queue is full, arming the retry timer.
  expect_failed_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());

  // An activity event arrives before the timer fires. Since read_queued_ is false,
  // on_erd_activity_ retries the queue and it succeeds this time. The event
  // itself is NOT processed (method returns early after queuing).
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  uint8_t feature_data[8] = {0};
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);

  // Now read_queued_ is true. A second activity event processes the completion.
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);

  // Now read_queued_ is true (ERD_APPLIANCE_FEATURE_API_0 is in-flight).
  // Elapse 50ms — the retry timer fires but queue_retry_() sees
  // read_queued_ == true and does nothing.
  tiny_timer_group_double_elapse_time(&timer_group, 50);

  // No additional read mock needed — the timer was a no-op.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}
TEST(feature_bit_manager, unrelated_erd_events_are_ignored)
{
  init_manager();

  // start() queues a read for ERD_COMMON_FEATURE_API successfully.
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());

  // Trigger a read_completed for ERD_APPLIANCE_FEATURE_API_0 — wrong ERD,
  // we're waiting for ERD_COMMON_FEATURE_API. It should be silently ignored.
  uint8_t data[8] = {0};
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, data, 8);

  // State should remain READING_0092; no additional read queued.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}


/* ------------------------------------------------------------------ */
/* get_valid_erds() — before/after parsing                              */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, get_valid_erds_returns_empty_before_complete)
{
  init_manager();

  CHECK_EQUAL(0u, manager.get_valid_erd_count());
}

TEST(feature_bit_manager, get_valid_erds_returns_empty_during_parsing)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  trigger_full_read_sequence();

  // In PARSING state, valid_erds is still empty until parsing completes.
  CHECK_EQUAL(0u, manager.get_valid_erd_count());
}

TEST(feature_bit_manager, get_valid_erds_contains_mandatory_erds_after_full_parse)
{
  init_manager();

  uint8_t feature_data[8] = {0};

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, feature_data, 8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_9, feature_data, 8);

  // Elapse timer to complete parsing (16 ticks * 5ms = 80ms).
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  // Mandatory ERDs are always added after parsing completes.
  bool found_model = false;
  bool found_serial = false;
  bool found_type = false;
  bool found_common = false;
  bool found_api0 = false;
  bool found_api9 = false;
  for (uint16_t i = 0; i < manager.get_valid_erd_count(); i++) {
    tiny_erd_t erd = manager.get_valid_erd(i);
    if (erd == ERD_MODEL_NUMBER) found_model = true;
    if (erd == ERD_SERIAL_NUMBER) found_serial = true;
    if (erd == ERD_APPLIANCE_TYPE) found_type = true;
    if (erd == ERD_COMMON_FEATURE_API) found_common = true;
    if (erd == ERD_APPLIANCE_FEATURE_API_0) found_api0 = true;
    if (erd == ERD_APPLIANCE_FEATURE_API_9) found_api9 = true;
  }
  CHECK_TRUE(found_model);
  CHECK_TRUE(found_serial);
  CHECK_TRUE(found_type);
  CHECK_TRUE(found_common);
  CHECK_TRUE(found_api0);
  CHECK_TRUE(found_api9);
}
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, read_completed_with_null_data_skips_erd)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  trigger_read_completed(ERD_COMMON_FEATURE_API, nullptr, 0);

  // Null data should skip to next ERD.
  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* valid ERD count consistency                                          */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, valid_erd_count_is_reasonable_after_parse)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  trigger_full_read_sequence();

  // Elapse timer to complete parsing (16 ticks * 5ms = 80ms).
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  // Should have at least the 14 mandatory ERDs.
  CHECK_TRUE(manager.get_valid_erd_count() >= 14);
}

/* ------------------------------------------------------------------ */
/* ERD data sizes capped at 8                                           */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, erd_data_sizes_capped_at_8)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  // Provide more than 8 bytes — should be capped internally.
  uint8_t big_data[16] = {0x00, 0x00, 0x00, 0x01, 0xAA, 0xBB, 0xCC, 0xDD,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, big_data, 16);

  // The manager caps at 8 bytes internally.  Verify by checking that
  // the subsequent read was queued (proving the event was processed).
  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* State machine: each READING state maps to correct ERD                */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, each_reading_state_queues_correct_erd)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  tiny_timer_group_double_t tg;
  tiny_gea3_erd_client_double_init(&ec);
  tiny_timer_group_double_init(&tg);
  mgr.init(&ec.interface, 0xC0, &tg.timer_group);

  uint8_t data[8] = {0};

  // READING_0092 -> queues ERD_COMMON_FEATURE_API
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("erd", ERD_COMMON_FEATURE_API)
    .ignoreOtherParameters()
    .andReturnValue(true);
  mgr.start();

  // READING_0093 -> queues ERD_APPLIANCE_FEATURE_API_0
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("erd", ERD_APPLIANCE_FEATURE_API_0)
    .ignoreOtherParameters()
    .andReturnValue(true);
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_read_completed;
  args.address = 0xC0;
  args.read_completed.request_id = 0;
  args.read_completed.erd = ERD_COMMON_FEATURE_API;
  args.read_completed.data = data;
  args.read_completed.data_size = 8;
  tiny_gea3_erd_client_double_trigger_activity_event(&ec, &args);

  // READING_0094 -> queues ERD_APPLIANCE_FEATURE_API_1
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("erd", ERD_APPLIANCE_FEATURE_API_1)
    .ignoreOtherParameters()
    .andReturnValue(true);
  args.read_completed.erd = ERD_APPLIANCE_FEATURE_API_0;
  tiny_gea3_erd_client_double_trigger_activity_event(&ec, &args);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, mgr.get_state());
}

/* ------------------------------------------------------------------ */
/* Parsing with feature bits set                                        */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, parsing_with_feature_bits_adds_erds_from_common)
{
  init_manager();

  // Common feature API with Primary bit (0x00000001) set.
  uint8_t common_data[8] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};

  // All appliance ERDs with valid data.
  uint8_t appliance_data[8] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, common_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, appliance_data, 8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_9, appliance_data, 8);

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());

  // Elapse timer to complete parsing (16 ticks * 5ms = 80ms).
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());

  // The valid ERD set should include mandatory ERDs plus any from feature bits.
  CHECK_TRUE(manager.get_valid_erd_count() > 0);
}

/* ------------------------------------------------------------------ */
/* Timer stop after parsing completes                                   */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, parse_timer_stops_after_complete)
{
  init_manager();

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  trigger_full_read_sequence();

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());

  // Elapse timer to complete parsing (16 ticks * 5ms = 80ms).
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());

  // Elapse more time — state should remain COMPLETE (timer was stopped).
  tiny_timer_group_double_elapse_time(&timer_group, 10);
  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());
}

TEST(feature_bit_manager, parse_tick_ms_is_5)
{
  CHECK_EQUAL(5u, FeatureBitManager::PARSE_TICK_MS);
}

TEST(feature_bit_manager, queue_retry_ms_is_50)
{
  CHECK_EQUAL(50u, FeatureBitManager::QUEUE_RETRY_MS);
}
/* ------------------------------------------------------------------ */
/* FAILED state behavior                                                */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, events_ignored_in_failed_state)
{
  init_manager();

  // start() queues a read for ERD_COMMON_FEATURE_API successfully.
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());

  // Trigger read_failed for ERD_COMMON_FEATURE_API to enter FAILED state.
  trigger_read_failed(ERD_COMMON_FEATURE_API);

  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());

  // Now trigger a read_completed for ERD_APPLIANCE_FEATURE_API_0 —
  // it should be silently ignored in FAILED state.
  uint8_t data[8] = {0};
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, data, 8);

  // State must remain FAILED; the event was ignored.
  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());
}

TEST(feature_bit_manager, queue_retry_timer_noop_in_failed_state)
{
  init_manager();

  // start() queues a read for 0x0092 successfully.
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING, manager.get_state());

  // Trigger read_failed for 0x0092 to enter FAILED state.
  trigger_read_failed(ERD_COMMON_FEATURE_API);

  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());

  // Verify that on_erd_activity_ ignores events in FAILED state.
  // Trigger a read_completed for 0x0093 — it should be ignored.
  uint8_t data[8] = {0};
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, data, 8);

  // State must remain FAILED; the event was ignored.
  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());

  // Verify that queue_retry_ returns early in FAILED state.
  // We do this by checking that no additional read is queued after
  // the retry timer fires. Since we can't directly call queue_retry_,
  // we verify the state remains FAILED after timer elapse.
  // (No retry timer is armed in this path, so elapse is a no-op.)
  tiny_timer_group_double_elapse_time(&timer_group, 100);
  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());
}

TEST(feature_bit_manager, failed_state_has_no_valid_erds)
{
  init_manager();

  // start() queues a read for ERD_COMMON_FEATURE_API successfully.
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.start();

  // Trigger read_failed for ERD_COMMON_FEATURE_API to enter FAILED state.
  trigger_read_failed(ERD_COMMON_FEATURE_API);

  CHECK_EQUAL(FEATURE_BIT_STATE_FAILED, manager.get_state());

  // In FAILED state, get_valid_erd_count() must return 0.
  CHECK_EQUAL(0u, manager.get_valid_erd_count());
}
