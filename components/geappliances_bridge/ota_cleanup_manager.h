// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Own the OTA-triggered HA discovery cleanup state machine and the
//       DiscoveryRefresh path, extracted from GeappliancesBridge.loop() to
//       reduce god-class size and simplify the main loop.
//
// Responsibilities:
//   - Detect when OTA cleanup or discovery refresh is needed
//   - Drive the cleanup → republish → reboot state machine
//   - Gate on steady state, MQTT initialized, and device ID complete
//   - Clear safe-mode counter and mark OTA valid before reboot
//
// NOT responsible for:
//   - HA discovery manager lifecycle (init/configure/start/destroy) —
//     those are called by this class but the struct is owned by the bridge.
//   - Reading discovery data hash from NVS — that happens in GeappliancesBridge::setup().
//
// Dependencies:
//   - ha_discovery_manager_t (owned by bridge, passed by reference)
//   - device_identity_manager_ (passed by reference)
//   - esphome_mqtt_client_adapter_t (passed by reference)
//   - esphome::App.safe_reboot() for final reboot
// =============================================================================

#pragma once

#include <functional>
#include <cstdint>

extern "C" {
#include "ha_discovery_manager.h"
#include "erd_cache.h"
}

#include "device_identity_manager.h"
#include "esphome_mqtt_client_adapter.h"

namespace esphome {
namespace geappliances_bridge {

class OtaCleanupManager {
 public:
  /// Initialize with references to bridge state needed by the state machine.
  /// @param ha_discovery_manager  Pointer to the bridge's HA discovery manager (owns cleanup sub-struct).
  /// @param device_identity_manager  Reference to device identity manager for device ID, model, serial, type.
  /// @param mqtt_client_adapter  Reference to MQTT client adapter for publish interface.
  /// @param erd_cache  Pointer to the shared ERD cache.
  /// @param generate_device_config  Whether device config generation is enabled.
  /// @param appliance_api_parsing  Whether appliance API ERD filtering is enabled.
  /// @param filter_config_topics  Whether config topic filtering is enabled.
  /// @param steady_state_reached  Reference to the bridge's steady state flag.
  /// @param mqtt_initialized  Reference to the bridge's MQTT initialized flag.
  /// @param reboot_callback  Called to perform the actual reboot (esphome::App.safe_reboot).
  void init(
      ha_discovery_manager_t* ha_discovery_manager,
      DeviceIdentityManager& device_identity_manager,
      esphome_mqtt_client_adapter_t& mqtt_client_adapter,
      erd_cache_t* erd_cache,
      bool generate_device_config,
      bool appliance_api_parsing,
      bool filter_config_topics,
      bool& steady_state_reached,
      bool& mqtt_initialized,
      std::function<void()> reboot_callback);

  /// Drive the cleanup state machine. Call from GeappliancesBridge::loop().
  void loop();

  /// Trigger OTA-triggered cleanup (called from setup() when OTA reboot is detected).
  void trigger_ota_cleanup();

  /// Trigger discovery refresh (called from DiscoveryRefreshButton or poll discovery complete).
  void trigger_discovery_refresh();

  /// Trigger initial HA discovery publish (called when steady state is first reached on a fresh install).
  void trigger_initial_discovery();

  /// Check for discovery changes (hash or device ID) and trigger cleanup if needed.
  /// Called from check_steady_state() when steady state is first reached.
  void check_discovery_changes(const char* current_device_id);

  /// Check if the appliance is ready for cleanup (steady state, MQTT, device ID).
  bool is_ready() const;

private:
  // NVS struct stored after each successful discovery publish.
  // Compared on next boot to detect changes requiring cleanup+republish.
  struct DiscoveryNVS {
    uint32_t version;
    uint32_t hash;
    char device_id[92];
    bool appliance_api_parsing;
    bool filter_config_topics;
  };

  enum CleanupTrigger { NONE, OTA, DISCOVERY_REFRESH, INITIAL };
  bool start_cleanup_();

  // State machine flags
  bool ota_cleanup_needed_{false};
  bool ota_cleanup_in_progress_{false};
  bool ota_discovery_publishing_{false};
  bool ota_reboot_pending_{false};
  [[maybe_unused]] uint32_t ota_reboot_start_ms_{0};
  bool discovery_refresh_in_progress_{false};
  bool initial_discovery_needed_{false};
  bool initial_discovery_done_{false};
  CleanupTrigger cleanup_trigger_{NONE};

  // References to bridge state
  ha_discovery_manager_t* ha_discovery_manager_{nullptr};
  DeviceIdentityManager* device_identity_manager_{nullptr};
  esphome_mqtt_client_adapter_t* mqtt_client_adapter_{nullptr};
  erd_cache_t* erd_cache_{nullptr};
  bool generate_device_config_{false};
  bool appliance_api_parsing_{false};
  bool filter_config_topics_{false};
  bool* steady_state_reached_{nullptr};
  bool* mqtt_initialized_{nullptr};

  // Reboot callback
  std::function<void()> reboot_callback_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
