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
//   - Remember across a reset when a cleanup removed the broker's configs and
//     the republish never completed, and retry it on the next boot (issue #203)
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
  struct DiscoveryRecord {
    uint32_t version;
    uint32_t hash;
    char device_id[92];
    bool appliance_api_parsing;
    bool filter_config_topics;
    uint8_t valid_erds_republishes;
    uint8_t valid_erds_stable_checks;
    uint32_t valid_erds_fingerprint;
  };

  static_assert(sizeof(DiscoveryRecord) == 108, "DiscoveryRecord on-NVS size changed");
  static_assert(offsetof(DiscoveryRecord, version) == 0, "DiscoveryRecord::version moved");
  static_assert(offsetof(DiscoveryRecord, hash) == 4, "DiscoveryRecord::hash moved");
  static_assert(offsetof(DiscoveryRecord, device_id) == 8, "DiscoveryRecord::device_id moved");
  static_assert(offsetof(DiscoveryRecord, appliance_api_parsing) == 100,
                "DiscoveryRecord::appliance_api_parsing moved");
  static_assert(offsetof(DiscoveryRecord, filter_config_topics) == 101,
                "DiscoveryRecord::filter_config_topics moved");
  static_assert(offsetof(DiscoveryRecord, valid_erds_republishes) == 102,
                "DiscoveryRecord::valid_erds_republishes moved");
  static_assert(offsetof(DiscoveryRecord, valid_erds_stable_checks) == 103,
                "DiscoveryRecord::valid_erds_stable_checks moved");
  static_assert(offsetof(DiscoveryRecord, valid_erds_fingerprint) == 104,
                "DiscoveryRecord::valid_erds_fingerprint moved");

  static constexpr size_t kRecordV4Size = 104;
  struct DiscoveryRecordV4 {
    uint8_t bytes[kRecordV4Size];
  };
  static_assert(sizeof(DiscoveryRecordV4) == kRecordV4Size, "v4 record size");
  static_assert(offsetof(DiscoveryRecord, valid_erds_republishes) == kRecordV4Size - 2,
                "v4 must be a prefix of v5: new fields start in v4's padding");

  static bool decode_record(const uint8_t* blob, size_t size, DiscoveryRecord* out);

  static constexpr uint8_t kMaxValidErdsRepublishes = 2;
  static constexpr uint8_t kValidErdsStableChecksToReset = 3;

  static constexpr uint32_t kRecordVersion = 5;

  static constexpr uint32_t kRebootDelayMs = 5000;

  // Upper bound on how long Cleaning or Publishing may go WITHOUT progress:
  // Cleaning measures from the last topic removed, Publishing from the last
  // successful publish. Both phases depend on the broker settling; if the
  // underlying module never reports done, the machine would otherwise reject
  // every later request for the rest of the boot, so a phase that stops making
  // progress has to be aborted.
  //
  // Measuring from progress rather than from phase entry is what makes the
  // bound safe: both modules retry dropped publishes through a backoff window
  // (HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS, HA_DISCOVERY_PUBLISH_RETRY_BACKOFF_MS)
  // and remove one topic / publish one entity per main-loop iteration, so a
  // congested broker stretches a run to (work items) x (window + loop period).
  // The largest discovery run (3605 entities) needs ~418 s at the fleet-typical
  // 29 ms loop period; cleaning needs the same headroom because its per-pass
  // queue (~150 topics) refills across passes until the device's ~3605 stale
  // configs are gone, and a loop spike to 150 ms pushes runs of those sizes
  // past any fixed entry-based bound while they are still working. A phase
  // that publishes nothing is wedged; one that is slow is not. The one wedge
  // this contract accepts: a publisher that re-announces stale topics faster
  // than removals can defer done indefinitely (ha_discovery_cleanup.md
  // limitation 8). On timeout the phase is aborted with no record saved, so
  // the next request (or the next boot's change check) retries.
  static constexpr uint32_t kPhaseTimeoutMs = 5u * 60u * 1000u;

  static constexpr uint8_t kMaxAbortReboots = 3;

  enum Trigger : uint8_t {
    kTriggerNone,
    kTriggerOta,
    kTriggerDiscoveryRefresh,
    kTriggerInitial,
    kTriggerPendingRepublish,
  };

  // The single lifecycle state. Replaces the seven independent booleans this
  // class used to carry, which were only valid in the handful of combinations
  // the loop() source order happened to produce.
  //
  //   Idle --trigger_*()--> Claimed --(trigger recorded)--> QueuedInitial / QueuedCleanup
  //   QueuedInitial --ready--> Publishing
  //   QueuedCleanup --ready--> Cleaning
  //   Cleaning --cleanup done--> Publishing
  //   Cleaning --kPhaseTimeoutMs with no topic removed--> Idle   (aborted, no record)
  //   Publishing --run failed--> RebootPending (budget left) / Idle (spent)
  //   Publishing --kPhaseTimeoutMs with no successful publish--> same as a failed run
  //   Publishing --run complete--> RebootPending
  //   RebootPending --kRebootDelayMs elapsed--> Idle
  enum Phase : uint8_t {
    kPhaseIdle,
    kPhaseQueuedInitial,
    kPhaseQueuedCleanup,
    kPhaseCleaning,
    kPhasePublishing,
    kPhaseRebootPending,
    kPhaseClaimed,
  };

  struct Hooks {
    void (*apply_mqtt_availability)(void* context, ha_discovery_manager_t* manager);
    bool (*load_discovery_record)(void* context, DiscoveryRecord* out);
    void (*save_discovery_record)(void* context, const DiscoveryRecord& record);
    void (*confirm_firmware)(void* context);
    uint8_t (*read_abort_reboot_count)(void* context);
    bool (*write_abort_reboot_count)(void* context, uint8_t count);
    bool (*read_publish_pending)(void* context);
    bool (*write_publish_pending)(void* context, bool pending);
    uint32_t (*read_largest_free_block)(void* context);
    void (*feed_watchdog)(void* context);
    void (*reboot)(void* context);
    void* context;
  };

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

  void loop();

  void trigger_ota_cleanup();

  void trigger_discovery_refresh();

  void trigger_initial_discovery();

  void check_discovery_changes(const char* current_device_id);

  void set_valid_erds_fingerprint(uint32_t fingerprint) { this->valid_erds_fingerprint_ = fingerprint; }
  uint32_t valid_erds_fingerprint() const { return this->valid_erds_fingerprint_; }

  bool is_ready() const;

  Phase phase() const { return static_cast<Phase>(phase_.load(std::memory_order_acquire)); }
  Trigger trigger() const { return trigger_; }
  bool initial_publish_done() const { return initial_publish_done_; }

  static const char* phase_name(Phase phase);
  const char* phase_name() const { return phase_name(phase()); }

 private:
  bool claim_request_(Phase target, Trigger trigger, const char* request);
  void trigger_pending_republish_();
  void set_phase_(Phase phase);
  void begin_cleanup_();
  void begin_publish_();
  void finish_publish_();
  void abort_publish_run_();

  ha_discovery_manager_t* ha_discovery_manager_{nullptr};
  DeviceIdentityManager* device_identity_manager_{nullptr};
  esphome_mqtt_client_adapter_t* mqtt_client_adapter_{nullptr};
  erd_cache_t* erd_cache_{nullptr};
  bool generate_device_config_{false};
  bool appliance_api_parsing_{false};
  bool filter_config_topics_{false};
  uint32_t valid_erds_fingerprint_{0};
  uint8_t next_valid_erds_republishes_{0};
  uint32_t cleanup_removed_{0};
  bool cleanup_completed_{false};

  bool (*is_ready_)(void* context) = nullptr;
  void* is_ready_context_{nullptr};
  uint32_t (*get_time_ms_)(void) = nullptr;
  Hooks hooks_{};

  std::atomic<uint8_t> phase_{static_cast<uint8_t>(kPhaseIdle)};
  Trigger trigger_{kTriggerNone};
  bool initial_publish_done_{false};
  uint32_t reboot_start_ms_{0};
  uint32_t phase_start_ms_{0};
  uint32_t progress_ms_{0};
  uint32_t progress_snapshot_{0};
};

}
}
