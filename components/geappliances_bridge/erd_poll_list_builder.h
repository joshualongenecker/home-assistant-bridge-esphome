/*!
 * @file
 * @brief Builds the ERD poll list based on bridge mode and configuration.
 *
 * Pure function — no HSM, no timers, no I/O.  Given the current operating mode,
 * subscription state, feature-bit results, custom ERDs, and appliance type,
 * returns a deduplicated list of ERDs that the polling bridge should probe.
 *
 * Decision logic:
 *
 *   SUBSCRIBE mode (subscription confirmed):
 *     → custom_erds only
 *
 *   POLL mode, appliance_api_parsing == true:
 *     → feature_bit_valid_erds + custom_erds
 *
 *   POLL mode, appliance_api_parsing == false:
 *     → commonErds + energyErds + applianceApiFeatureErds
 *       + appliance-specific ERDs (by appliance_type)
 *       + custom_erds
 *
 *   AUTO mode, subscription active:
 *     → custom_erds only (same as SUBSCRIBE)
 *
 *   AUTO mode, subscription not active (fallback):
 *     → same as POLL mode with current appliance_api_parsing setting
 *
 * The returned list is deduplicated.  Order is: standard ERDs first
 * (in their original group order), then custom ERDs.
 */

#pragma once
#include <cstdint>

#include "tiny_erd.h"
#include "erd_lists.h"
#include "bridge_mode.h"
#include "erd_bridge_common.h"

namespace esphome {
namespace geappliances_bridge {

/* Maximum number of ERDs in a poll list.  Uses POLLING_LIST_MAX_SIZE from erd_lists.h. */
#define ERD_POLL_LIST_MAX_SIZE POLLING_LIST_MAX_SIZE

/*
 * Configuration for building the ERD poll list.
 */
struct ErdPollListConfig {
  /// The operating mode (POLL, SUBSCRIBE, or AUTO).
  BridgeMode mode;

  /// Whether subscription is currently active and confirmed.
  bool subscription_active;

  /// Whether appliance API feature bit filtering is enabled.
  /// When true, only ERDs reported by the feature bits are polled.
  bool appliance_api_parsing;

  /// The valid ERD set produced by the feature bit manager.
  /// Raw pointer into the manager's fixed array; NULL if not available.
  const tiny_erd_t* feature_bit_valid_erds;
  uint16_t feature_bit_valid_erds_count;
  /// User-configured custom ERDs with optional per-ERD board addresses.
  /// board_address == PROBE_ENTRY_DEFAULT_ADDRESS means "use primary host address".
  const probe_entry_t* custom_erds;
  uint16_t custom_erds_count;

  /// The discovered appliance type (0-255).
  /// Used to look up appliance-specific ERDs from erd_lists.h.
  uint8_t appliance_type;
};

/*
 * The result of building the poll list.
 * Uses a fixed-capacity array to avoid heap allocation.
 */
struct ErdPollListResult {
  /// The list of ERDs to probe with per-ERD board addresses.
  probe_entry_t erds[ERD_POLL_LIST_MAX_SIZE];
  uint16_t erds_count;

  /// Human-readable description of how the list was built (for logging).
  const char* description;
};

/*
 * Build the list of ERDs the polling bridge should probe.
 */
ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config);

}  // namespace geappliances_bridge
}  // namespace esphome
