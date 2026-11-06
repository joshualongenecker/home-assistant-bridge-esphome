
// Owns the OTA-triggered HA discovery cleanup state machine (cleanup ->
// republish -> reboot), extracted from GeappliancesBridge::loop().
//
// The ha_discovery_manager_t is owned by the bridge, not this class: init/
// configure/start/destroy are called here but the struct's storage lives in
// the bridge. The discovery-data hash is read from NVS in
// GeappliancesBridge::setup(), not here.
#pragma once

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
      void (*reboot_callback)());

  void loop();

  void trigger_ota_cleanup();

  void trigger_discovery_refresh();

  void trigger_initial_discovery();

  void check_discovery_changes(const char* current_device_id);

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

  bool ota_cleanup_needed_{false};
  bool ota_cleanup_in_progress_{false};
  bool ota_discovery_publishing_{false};
  bool ota_reboot_pending_{false};
  [[maybe_unused]] uint32_t ota_reboot_start_ms_{0};
  bool discovery_refresh_in_progress_{false};
  bool initial_discovery_needed_{false};
  bool initial_discovery_done_{false};
  CleanupTrigger cleanup_trigger_{NONE};

  ha_discovery_manager_t* ha_discovery_manager_{nullptr};
  DeviceIdentityManager* device_identity_manager_{nullptr};
  esphome_mqtt_client_adapter_t* mqtt_client_adapter_{nullptr};
  erd_cache_t* erd_cache_{nullptr};
  bool generate_device_config_{false};
  bool appliance_api_parsing_{false};
  bool filter_config_topics_{false};
  bool* steady_state_reached_{nullptr};
  bool* mqtt_initialized_{nullptr};

  void (*reboot_callback_)() = nullptr;
};

}  // namespace geappliances_bridge
}  // namespace esphome
