/*!
 * @file
 * @brief Polls ERDs and updates the ERD cache (polling mode)
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Periodically poll a list of ERDs from the appliance and update their
//       values in the ERD cache; fulfill write commands received from MQTT.
//
// Responsibilities:
//   - Maintain and iterate a fixed-capacity polling list
//   - Drive a tiny_hsm for probe discovery, polling, and appliance-lost recovery
//   - Accept a pre-built probe list to verify ERDs before polling
//   - Report polling health metrics (cycle count, last cycle time)
//
// NOT responsible for:
//   - Subscription-mode operation (see erd_bridge_subscribe.h)
//   - Bridge initialization or startup phase management
//
// ---- 3-Phase Polling Lifecycle ----
//
// Phase 1 — Build Verification List (no reads)
//   Determines which ERDs to probe in Phase 2. Contents depend on config:
//   - POLL or AUTO→poll, appliance_api_parsing=false:
//       commonErds + energyErds + applianceApiFeatureErds +
//       appliance-specific (erd_lists.h) + custom_erds
//   - POLL or AUTO→poll, appliance_api_parsing=true:
//       feature_bit_manager valid ERD list + custom_erds
//   - SUBSCRIBE or AUTO→subscribing:
//       custom_erds only (empty if none configured)
//
// Phase 2 — Verification (sequential reads; HSM states before state_polling)
//   Each ERD in the verification list is read once:
//   - read_completed             → register ERD + publish value → next ERD
//   - read_failed(not_supported) → permanently exclude          → next ERD
//   - read_failed(retries_exhausted) → skip, not added to polling list → next ERD
//   Result: erd_polling_list contains only ERDs that responded successfully.
//
// Phase 3 — Steady-State Polling (state_polling)
//   All registered ERDs are read sequentially each cycle. Timer semantics:
//   - Timer fires mid-cycle (cycle not yet complete):
//       set restart_pending=true; let the cycle finish naturally.
//   - Timer fires after cycle already complete:
//       start next cycle immediately; re-arm timer.
//   - Cycle completes with restart_pending=true:
//       start next cycle immediately; re-arm timer; clear restart_pending.
//   - Cycle completes, timer still armed:
//       wait for timer to fire.
//   - Cycle completes, timer not armed, no restart_pending:
//       start next cycle immediately; re-arm timer.
//
// Dependencies:
//   - i_tiny_gea3_erd_client.h, tiny_hsm.h, tiny_timer.h
//   - erd_lists.h (appliance ERD list arrays)
// =============================================================================

#ifndef erd_bridge_poll_h
#define erd_bridge_poll_h

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"
#include "erd_cache.h"
#include "erd_lists.h"
}
#include "erd_bridge_common.h"

typedef struct {
  tiny_erd_t erd_polling_list[POLLING_LIST_MAX_SIZE];
  uint8_t erd_polling_addresses[POLLING_LIST_MAX_SIZE];
  uint16_t polling_list_count;
  uint32_t polling_interval_ms;
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  tiny_timer_t appliance_lost_timer;
  tiny_timer_t polling_timer;
  tiny_event_subscription_t erd_client_activity_subscription;
  tiny_hsm_t hsm;
  erd_set_t erd_set;
  erd_cache_t* erd_cache;
  tiny_gea3_erd_client_request_id_t request_id;
  uint8_t erd_host_address;
  const probe_entry_t* appliance_erd_list;
  uint16_t appliance_erd_list_count;
  uint16_t erd_index;
  /* Number of ERDs in the current polling cycle that have completed (success
   * or failure).  Used together with erd_index to determine when a full cycle
   * has finished — the cycle only restarts when cycle_completed_count equals
   * polling_list_count AND the polling timer has expired. */
  uint16_t cycle_completed_count;
  /* Set to true once the HSM transitions into state_polling (all ERD
   * discovery phases have completed). Reset to false on appliance loss/
   * reconnect. */
  bool polling_list_complete;
  /* Current HSM state as an enum (see polling_state_t).
   * Callers may watch this for state transitions to emit
   * diagnostic log messages without coupling to ESP logging headers. */
  polling_state_t current_state;
  /* Consecutive cycle failure counter. Incremented on each full cycle where
   * all ERDs fail. Reset on any successful read. Transitions to state_failed
   * when this reaches 3, matching the subscription bridge's failure threshold. */
  uint8_t polling_failure_count;
  /* True if any ERD in the current polling cycle has failed.
   * Reset at the start of each cycle; checked on cycle completion
   * to increment the consecutive failure counter. */
  bool cycle_has_failure;
  /* Pre-built list of ERDs to probe during discovery.
   * Set by the caller before erd_bridge_poll_init(); the bridge copies
   * successfully-probed ERDs into erd_polling_list during the probe phase. */
  const probe_entry_t* probe_list;
  uint16_t probe_list_count;
  /* Stores the pre-known appliance address so that on appliance loss
   * the bridge can re-probe at the correct address. */
  uint8_t known_host_address;
  /* Health metrics: updated by the polling bridge as cycles complete.
   * cycle_start_ms: millis() when the current cycle's first read was sent.
   * last_cycle_time_ms: duration of the last completed cycle (ms).
   * cycle_count: total completed cycles since init. */
  uint32_t cycle_start_ms;
  uint32_t last_cycle_time_ms;
  uint32_t cycle_count;
  /* True while the polling timer is armed (between arm_polling_timer and the
   * next signal_polling_timer_expired).  Used to decide whether to restart
   * polling immediately when a cycle finishes: if the timer is armed, wait for
   * it to fire; if not, start the next cycle right away. */
  bool polling_timer_armed;
  /* Set to true when the polling_interval timer fires while a cycle is still
   * in progress (cycle_completed_count < polling_list_count).  The in-progress
   * cycle is allowed to finish, then the cycle-completion handler restarts
   * immediately instead of waiting for another timer interval. */
  bool restart_pending;
  /* True while a cycle's read requests are being sent in budgeted chunks.
   * When set, the polling timer handler resumes sending instead of starting
   * a new cycle.  Cleared once all ERD reads for the cycle are queued. */
  bool cycle_sending_in_progress;
  /* Called once when the HSM enters state_polling (discovery complete).
   * The callback may send a signal to the startup HSM to transition to the
   * next phase.  Set after erd_bridge_poll_init() and before the HSM
   * processes its first signal.  NULL means no callback. */
  void (*on_discovery_complete)(void* context);
  void* on_discovery_complete_context;
} erd_bridge_poll_t;

/*!
 * Initialize the ERD polling bridge.
 */
void erd_bridge_poll_init(
  erd_bridge_poll_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint32_t polling_interval_ms,
  uint8_t host_address,
  const probe_entry_t* probe_list,
  uint16_t probe_list_count,
  erd_cache_t* cache);

/*!
 * Destroy the ERD polling bridge.
 */
void erd_bridge_poll_destroy(erd_bridge_poll_t* self);

#endif
