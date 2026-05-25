/*!\
 * @file
 * @brief Home Assistant device-discovery publishing.
 *
 * Delegates to HaDiscoveryManager for all HA discovery logic.
 * This file retains only the bridge-facing delegation method.
 */

#include "geappliances_bridge.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "geappliances_bridge";

// ---------------------------------------------------------------------------
// on_ha_discovery_erd_seen_() — called when a new ERD is seen via subscription
// ---------------------------------------------------------------------------

void GeappliancesBridge::on_ha_discovery_erd_seen_(tiny_erd_t erd)
{
  this->ha_discovery_manager_.on_erd_seen(erd);
}

}  // namespace geappliances_bridge
}  // namespace esphome
