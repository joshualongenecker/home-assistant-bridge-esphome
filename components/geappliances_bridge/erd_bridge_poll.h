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

typedef enum {
  poll_cycle_phase_idle,
  poll_cycle_phase_sending,
  poll_cycle_phase_awaiting,
  poll_cycle_phase_awaiting_restart
} poll_cycle_phase_t;

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
  uint16_t cycle_completed_count;
  polling_state_t current_state;
  poll_cycle_phase_t cycle_phase;
  /* Consecutive cycle failure counter. Incremented on each full cycle where
   * all ERDs fail. Reset on any successful read. Transitions to state_failed
   * when this reaches 3, matching the subscription bridge's failure threshold. */
  uint8_t polling_failure_count;
  bool cycle_has_failure;
  const probe_entry_t* probe_list;
  uint16_t probe_list_count;
  uint8_t known_host_address;
  uint32_t cycle_start_ms;
  uint32_t last_cycle_time_ms;
  uint32_t cycle_count;
  void (*on_discovery_complete)(void* context);
  void* on_discovery_complete_context;
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

void erd_bridge_poll_set_time_fn(
  erd_bridge_poll_t* self,
  uint32_t (*get_time_ms)(void));
void erd_bridge_poll_set_yield_fn(
  erd_bridge_poll_t* self,
  void (*yield_fn)(void));

void erd_bridge_poll_destroy(erd_bridge_poll_t* self);

#endif
