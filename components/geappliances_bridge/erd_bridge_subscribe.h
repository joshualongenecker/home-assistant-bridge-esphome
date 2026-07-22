/*!
 * @file
 * @brief Subscribes to ERDs and pushes published values to the ERD cache.
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Subscribe to all ERDs at the appliance's address and relay their
//       published values to the ERD cache.
//
// Responsibilities:
//   - Manage a tiny_hsm that drives GEA3 subscription lifecycle
//   - Forward ERD publications to erd_cache
//
// NOT responsible for:
//   - Polling (see erd_bridge_poll.h)
//   - Write requests or MQTT interaction
//   - Bridge initialization or startup phase management
//
// Dependencies:
//   - i_tiny_gea3_erd_client.h, tiny_hsm.h, tiny_timer.h
// =============================================================================

#ifndef erd_bridge_subscribe_h
#define erd_bridge_subscribe_h

struct erd_cache_t;

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"
}
#include "erd_bridge_common.h"

typedef struct {
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  tiny_timer_t timer;
  tiny_timer_t quiet_timer;
  tiny_event_subscription_t erd_client_activity_subscription;
  erd_set_t erd_set;
  erd_cache_t* erd_cache;
  // Opaque pointer to ErdRegistry for valid-ERD filtering.
  // Set via erd_bridge_subscribe_set_erd_registry(). Cast in .cpp.
  void* erd_registry;
  tiny_hsm_t hsm;
  uint8_t erd_host_address;
  subscription_state_t current_state;
  uint8_t subscribe_failure_count;
} erd_bridge_subscribe_t;

/*!
 * Initialize the ERD subscription bridge.
 */
void erd_bridge_subscribe_init(
  erd_bridge_subscribe_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint8_t address,
  erd_cache_t* cache);

/*!
 * Destroy the ERD subscription bridge.
 */
void erd_bridge_subscribe_destroy(
  erd_bridge_subscribe_t* self);

/*!
 * Set the ERD registry for filtering subscription publications.
 * Only ERDs considered valid by the registry are forwarded to the cache.
 * Pass nullptr to disable filtering.
 */
void erd_bridge_subscribe_set_erd_registry(
  erd_bridge_subscribe_t* self,
  void* erd_registry);

#endif
