

#ifndef erd_bridge_subscribe_h
#define erd_bridge_subscribe_h

struct erd_cache_t;

namespace esphome {
namespace geappliances_bridge {
class ErdRegistry;
}  // namespace geappliances_bridge
}  // namespace esphome

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
  // Valid-ERD filter (feature-bit set + custom ERDs). Owned by
  // GeappliancesBridge and outlives the subscribe bridge; set via
  // erd_bridge_subscribe_set_erd_registry(). nullptr disables filtering.
  esphome::geappliances_bridge::ErdRegistry* erd_registry;
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
  esphome::geappliances_bridge::ErdRegistry* erd_registry);

#endif
