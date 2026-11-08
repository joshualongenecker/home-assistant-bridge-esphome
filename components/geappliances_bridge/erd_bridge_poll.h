
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
// Polling lifecycle:
//   1. Probe: read the appliance ERD list to discover which ERDs exist; non-responders
//      are filtered out in the same pass (feature-bit filtering happens at list-build time).
//   2. Poll: steady-state polling of the verified list.
//
// Poll-phase timer semantics:
//   Timer fires mid-cycle (cycle not yet complete): set restart_pending=true;
//   let the cycle finish naturally.
//   Timer fires after cycle already complete: start next cycle immediately;
//   re-arm timer.
//   Cycle completes with restart_pending=true: start next cycle immediately;
//   re-arm timer; clear restart_pending.

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
  /* Time and yield injection — mirrors erd_cache_mqtt_publisher_t::get_time_ms.
   * Defaults are set in erd_bridge_poll_init() (esphome::millis / delay(0));
   * tests install fakes via the setters so the budgeted send/resume paths
   * (POLL_YIELD_MS, POLL_CYCLE_SEND_BUDGET_MS, cycle_sending_in_progress)
   * are exercised against a deterministic clock instead of the HAL double's
   * frozen counter. */
  uint32_t (*get_time_ms)(void);
  void (*yield_fn)(void);
} erd_bridge_poll_t;

void erd_bridge_poll_init(
  erd_bridge_poll_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint32_t polling_interval_ms,
  uint8_t host_address,
  const probe_entry_t* probe_list,
  uint16_t probe_list_count,
  erd_cache_t* cache);

/* Override the time source / yield after init (test seam).  MUST be called
 * after erd_bridge_poll_init(): init installs the HAL defaults
 * unconditionally (the struct is a garbage-initialized plain member and
 * nothing on the hardware path pre-sets these fields, so pre-init values
 * are not preserved).  A NULL argument restores the default. */
void erd_bridge_poll_set_time_fn(
  erd_bridge_poll_t* self,
  uint32_t (*get_time_ms)(void));
void erd_bridge_poll_set_yield_fn(
  erd_bridge_poll_t* self,
  void (*yield_fn)(void));

void erd_bridge_poll_destroy(erd_bridge_poll_t* self);

#endif
