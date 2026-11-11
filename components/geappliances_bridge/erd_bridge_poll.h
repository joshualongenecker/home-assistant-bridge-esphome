
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
// Poll-phase cycle machine (cycle_phase — exactly one phase active at a time):
//   idle              interval timer armed, no cycle in flight; timer expiry
//                     starts the next cycle.
//   sending           budgeted sends in progress, resume timer armed; timer
//                     expiry resumes sending.
//   awaiting          all reads queued, interval timer armed; timer expiry
//                     means the interval elapsed mid-cycle → awaiting_restart.
//   awaiting_restart  interval timer fired while reads were in flight; the
//                     cycle finishes naturally, then the next cycle starts
//                     immediately (no timer armed until the restart).
//   A cycle completing in `awaiting` returns to `idle` (the interval timer is
//   still armed, so the next cycle waits for it to fire); a cycle completing
//   in `awaiting_restart` restarts immediately.

/* Cycle phase within state_polling.  Replaces the former polling_timer_armed /
 * restart_pending / cycle_sending_in_progress flag cluster: the phase
 * enumerates exactly the valid timer/cycle combinations, so an invalid
 * combination is unrepresentable. */
typedef enum {
  poll_cycle_phase_idle,
  poll_cycle_phase_sending,
  poll_cycle_phase_awaiting,
  poll_cycle_phase_awaiting_restart
} poll_cycle_phase_t;

static inline const char* poll_cycle_phase_name(poll_cycle_phase_t phase)
{
  switch(phase) {
    case poll_cycle_phase_idle: return "idle";
    case poll_cycle_phase_sending: return "sending";
    case poll_cycle_phase_awaiting: return "awaiting";
    case poll_cycle_phase_awaiting_restart: return "awaiting_restart";
    default: return "unknown";
  }
}

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
   * has finished — the cycle restarts once cycle_completed_count equals
   * polling_list_count; the cycle phase then decides idle vs immediate restart. */
  uint16_t cycle_completed_count;
  /* Current HSM state as an enum (see polling_state_t).
   * Callers may watch this for state transitions to emit
   * diagnostic log messages without coupling to ESP logging headers. */
  polling_state_t current_state;
  /* Cycle phase within state_polling (see poll_cycle_phase_t).  Encodes
   * exactly which timer is armed and whether a cycle is in flight,
   * replacing the former polling_timer_armed / restart_pending /
   * cycle_sending_in_progress flag cluster (and polling_list_complete,
   * which duplicated current_state == polling_state_polling). */
  poll_cycle_phase_t cycle_phase;
  /* Consecutive cycle failure counter. Incremented on each full cycle where
   * all ERDs fail. Reset on any successful read. Transitions to state_failed
   * when this reaches 3, matching the subscription bridge's failure threshold. */
  uint8_t polling_failure_count;
  /* True if any ERD in the current polling cycle has failed.
   * Cleared at cycle completion; checked there to increment the
   * consecutive failure counter. */
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
  /* Called once when the HSM enters state_polling (discovery complete).
   * The callback may send a signal to the startup HSM to transition to the
   * next phase.  Set after erd_bridge_poll_init() and before the HSM
   * processes its first signal.  NULL means no callback. */
  void (*on_discovery_complete)(void* context);
  void* on_discovery_complete_context;
  /* Time and yield injection — mirrors erd_cache_mqtt_publisher_t::get_time_ms.
   * Defaults are set in erd_bridge_poll_init() (esphome::millis / delay(0));
   * tests install fakes via the setters so the budgeted send/resume paths
   * (POLL_YIELD_MS, POLL_CYCLE_SEND_BUDGET_MS, the sending phase)
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
