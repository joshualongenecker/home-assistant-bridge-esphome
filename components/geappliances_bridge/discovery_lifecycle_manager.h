// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Own the HA-discovery lifecycle as one explicit phase machine: the
//       initial publish on a fresh install, the cleanup + republish paths
//       (OTA, discovery-affecting config change, Discovery Refresh button),
//       and the reboot that follows a successful publish.
//
// Responsibilities:
//   - Hold exactly one DiscoveryPhase for all discovery work
//   - Decide from the persisted discovery record whether a republish is due
//   - Drive ha_discovery_manager_t and ha_discovery_cleanup_t to completion
//
// NOT responsible for:
//   - Building or publishing discovery payloads (ha_discovery_manager)
//   - Deleting stale topics (ha_discovery_cleanup)
//   - NVS access, OTA-rollback confirmation, watchdog feeding, rebooting, or
//     reading ESPHome's MQTT availability config. Those are host-environment
//     effects supplied through DiscoveryHooks so the phase machine runs
//     identically on the device and in the unit-test harness.
//
// Dependencies:
//   - ha_discovery_manager.h, ha_discovery_cleanup.h, erd_cache.h
//   - DeviceIdentityManager, esphome_mqtt_client_adapter
// =============================================================================

// Extracted from GeappliancesBridge::loop(). The ha_discovery_manager_t is
// owned by the bridge, not this class: init/configure/start/destroy are called
// here but the struct's storage lives in the bridge. The discovery-data hash
// is read from NVS through the injected hooks, not here.
#pragma once

#include <atomic>
#include <cstddef>

extern "C" {
#include "ha_discovery_manager.h"
#include "erd_cache.h"
}

#include "device_identity_manager.h"
#include "esphome_mqtt_client_adapter.h"

namespace esphome {
namespace geappliances_bridge {

class DiscoveryLifecycleManager {
 public:
  // Record stored in NVS after each successful discovery publish and compared
  // on the next boot to detect changes requiring cleanup + republish.
  // Field order and sizes are the on-NVS layout: changing any of them
  // requires bumping kRecordVersion so old records are republished once.
  struct DiscoveryRecord {
    uint32_t version;
    uint32_t hash;
    char device_id[92];
    bool appliance_api_parsing;
    bool filter_config_topics;
  };

  // The on-NVS layout is an OTA-compatibility contract: records written by a
  // previous firmware must load with the same field offsets, so the layout is
  // pinned here. A static_assert failure means either the record changed
  // intentionally (bump kRecordVersion so old records are republished once)
  // or accidentally (fix the field).
  static_assert(sizeof(DiscoveryRecord) == 104, "DiscoveryRecord on-NVS size changed");
  static_assert(offsetof(DiscoveryRecord, version) == 0, "DiscoveryRecord::version moved");
  static_assert(offsetof(DiscoveryRecord, hash) == 4, "DiscoveryRecord::hash moved");
  static_assert(offsetof(DiscoveryRecord, device_id) == 8, "DiscoveryRecord::device_id moved");
  static_assert(offsetof(DiscoveryRecord, appliance_api_parsing) == 100,
                "DiscoveryRecord::appliance_api_parsing moved");
  static_assert(offsetof(DiscoveryRecord, filter_config_topics) == 101,
                "DiscoveryRecord::filter_config_topics moved");

  // Version 3 adds persisted discovery-affecting config flags. Version 4 adds
  // the availability topic to every payload.
  static constexpr uint32_t kRecordVersion = 4;

  // Grace period between a successful publish and the reboot, so the last
  // discovery packets leave through MQTT before the reset.
  static constexpr uint32_t kRebootDelayMs = 5000;

  // Upper bound on how long Cleaning or Publishing may stay in flight. Both
  // phases depend on the broker settling; if the underlying module never
  // reports done, the machine would otherwise reject every later request for
  // the rest of the boot. On timeout the phase is aborted back to Idle with
  // no record saved, so the next request (or the next boot's change check)
  // retries. Generous enough that no healthy run comes close: a full
  // cleanup + publish converges in seconds.
  static constexpr uint32_t kPhaseTimeoutMs = 5u * 60u * 1000u;

  // Why the current phase was entered. Orthogonal to the phase: the same
  // cleanup -> publish -> reboot sequence runs for every trigger, and a few
  // transitions depend on which one started it.
  enum Trigger : uint8_t {
    kTriggerNone,
    kTriggerOta,                // stale topics from a previous layout must go
    kTriggerDiscoveryRefresh,   // user pressed the Discovery Refresh button
    kTriggerInitial,            // fresh install: publish, no cleanup
  };

  // The single lifecycle state. Replaces the seven independent booleans this
  // class used to carry, which were only valid in the handful of combinations
  // the loop() source order happened to produce.
  //
  //   Idle --trigger_*()--> Claimed --(trigger recorded)--> QueuedInitial / QueuedCleanup
  //   QueuedInitial --ready--> Publishing
  //   QueuedCleanup --ready--> Cleaning
  //   Cleaning --cleanup done--> Publishing
  //   Cleaning --kPhaseTimeoutMs elapsed--> Idle   (aborted, no record)
  //   Publishing --run failed--> Idle              (no record saved, no reboot)
  //   Publishing --kPhaseTimeoutMs elapsed--> Idle (aborted, no record)
  //   Publishing --run complete--> RebootPending
  //   RebootPending --kRebootDelayMs elapsed--> Idle
  enum Phase : uint8_t {
    kPhaseIdle,           // nothing running; accepts the next request
    kPhaseQueuedInitial,  // initial publish requested, waiting for readiness
    kPhaseQueuedCleanup,  // cleanup + republish requested, waiting for readiness
    kPhaseCleaning,       // deleting stale discovery topics
    kPhasePublishing,     // a discovery run is in flight
    kPhaseRebootPending,  // published and recorded; rebooting after the delay
    // Transient: a trigger won the atomic claim from Idle and is writing
    // trigger_ before publishing the queued phase. Only observable from
    // loop() if a trigger runs off the main loop; it exists so the
    // check-then-act of "accept only from Idle" cannot let two triggers both
    // be accepted. loop() treats it as busy and waits one iteration.
    kPhaseClaimed,
  };

  // Host-environment effects. Every entry has a production default (installed
  // by init() when left NULL) and a no-op default in the unit-test harness, so
  // the phase machine never branches on whether an effect is available.
  struct Hooks {
    // Adds ESPHome's birth/LWT availability to the next discovery payloads.
    void (*apply_mqtt_availability)(void* context, ha_discovery_manager_t* manager);
    // Loads the persisted record; false when nothing is stored.
    bool (*load_discovery_record)(void* context, DiscoveryRecord* out);
    // Stores the record and flushes NVS.
    void (*save_discovery_record)(void* context, const DiscoveryRecord& record);
    // Clears the safe-mode counter and cancels the OTA rollback.
    void (*confirm_firmware)(void* context);
    // Feeds the task watchdog while waiting out the reboot delay.
    void (*feed_watchdog)(void* context);
    // Restarts the device.
    void (*reboot)(void* context);
    void* context;
  };

  // is_ready / is_ready_context report the bridge's own readiness (steady
  // state reached and MQTT initialized). They replace the bool& references to
  // GeappliancesBridge members this class used to alias.
  void init(
      ha_discovery_manager_t* ha_discovery_manager,
      DeviceIdentityManager& device_identity_manager,
      esphome_mqtt_client_adapter_t& mqtt_client_adapter,
      erd_cache_t* erd_cache,
      bool generate_device_config,
      bool appliance_api_parsing,
      bool filter_config_topics,
      bool (*is_ready)(void* context),
      void* is_ready_context,
      const Hooks& hooks = Hooks{});

  // Advances the phase machine by at most one transition per call.
  void loop();

  void trigger_ota_cleanup();

  void trigger_discovery_refresh();

  void trigger_initial_discovery();

  // Compares the persisted record with the current discovery inputs and queues
  // the matching action: initial publish when nothing is stored, cleanup +
  // republish when the version, hash, device ID or flags changed.
  void check_discovery_changes(const char* current_device_id);

  // Bridge readiness plus a completed device identity.
  bool is_ready() const;

  Phase phase() const { return static_cast<Phase>(phase_.load(std::memory_order_acquire)); }
  Trigger trigger() const { return trigger_; }
  bool initial_publish_done() const { return initial_publish_done_; }

  static const char* phase_name(Phase phase);
  const char* phase_name() const { return phase_name(phase()); }

 private:
  // Atomically claims the machine from Idle: on success trigger_ is set and
  // the phase is published as target; on failure logs why and returns false.
  bool claim_request_(Phase target, Trigger trigger, const char* request);
  void set_phase_(Phase phase);
  // Configures and starts the topic cleanup; stays QueuedCleanup if not ready.
  void begin_cleanup_();
  // Resets, configures and starts a discovery run.
  void begin_publish_();
  // Drives a run to completion; records the outcome and either returns to
  // Idle (failure) or arms the reboot (success).
  void finish_publish_();

  ha_discovery_manager_t* ha_discovery_manager_{nullptr};
  DeviceIdentityManager* device_identity_manager_{nullptr};
  esphome_mqtt_client_adapter_t* mqtt_client_adapter_{nullptr};
  erd_cache_t* erd_cache_{nullptr};
  bool generate_device_config_{false};
  bool appliance_api_parsing_{false};
  bool filter_config_topics_{false};

  bool (*is_ready_)(void* context) = nullptr;
  void* is_ready_context_{nullptr};
  uint32_t (*get_time_ms_)(void) = nullptr;
  Hooks hooks_{};

  // Written through claim_request_/set_phase_ only. Atomic because
  // trigger_discovery_refresh() may run off the main loop (the Discovery
  // Refresh button's press_action is MQTT-command-driven), while loop() and
  // every other transition run on it. trigger_ is published by the release
  // store on phase_; readers acquire first.
  std::atomic<uint8_t> phase_{static_cast<uint8_t>(kPhaseIdle)};
  Trigger trigger_{kTriggerNone};
  // Sticky: set once a publish has succeeded, so a fresh install is not
  // republished a second time in the same boot.
  bool initial_publish_done_{false};
  uint32_t reboot_start_ms_{0};
  // When Cleaning/Publishing was entered; basis for the kPhaseTimeoutMs check.
  uint32_t phase_start_ms_{0};
};

}  // namespace geappliances_bridge
}  // namespace esphome
