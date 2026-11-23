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
    // Fill what was padding in v4 (see kMaxValidErdsRepublishes):
    // consecutive republishes that only a valid-ERD fingerprint change caused,
    // and consecutive change checks since then that saw the fingerprint
    // unchanged
    uint8_t valid_erds_republishes;
    uint8_t valid_erds_stable_checks;
    // Fingerprint of the host board and the valid-ERD set discovery was
    // filtered by (see set_valid_erds_fingerprint()); 0 = no filter / unknown
    uint32_t valid_erds_fingerprint;
  };

  // The on-NVS layout is an OTA-compatibility contract: records written by a
  // previous firmware must load with the same field offsets, so the layout is
  // pinned here. A static_assert failure means either the record changed
  // intentionally (bump kRecordVersion so old records are republished once)
  // or accidentally (fix the field).
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

  // A version 4 record: the layout above up to filter_config_topics, padded to
  // 104 bytes. Version 5 only appends to it, so it is a prefix of
  // DiscoveryRecord. The loader decodes one when the current layout doesn't
  // fit, so an upgraded device sees a stale version (cleanup + republish)
  // instead of no record at all (publish without cleanup).
  static constexpr size_t kRecordV4Size = 104;
  struct DiscoveryRecordV4 {
    uint8_t bytes[kRecordV4Size];
  };
  static_assert(sizeof(DiscoveryRecordV4) == kRecordV4Size, "v4 record size");
  static_assert(offsetof(DiscoveryRecord, valid_erds_republishes) == kRecordV4Size - 2,
                "v4 must be a prefix of v5: new fields start in v4's padding");

  // Decode a stored record blob of either layout: sizeof(DiscoveryRecord)
  // (current) or kRecordV4Size (its version field stays 4, so the change
  // check cleans up and republishes once). Any other size is no record.
  static bool decode_record(const uint8_t* blob, size_t size, DiscoveryRecord* out);

  // A valid-ERD set that changes on every boot (partial feature-bit reads)
  // must not republish and reboot forever: after this many republishes in a
  // row caused only by the fingerprint, further fingerprint changes are
  // logged and ignored until the fingerprint has been seen unchanged at
  // kValidErdsStableChecksToReset change checks in a row (a flapping set
  // never gets there: every mismatch restarts the streak), or until Discovery
  // Refresh or another discovery change republishes.
  static constexpr uint8_t kMaxValidErdsRepublishes = 2;
  static constexpr uint8_t kValidErdsStableChecksToReset = 3;

  // Version 3 adds persisted discovery-affecting config flags. Version 4 adds
  // the availability topic to every payload. Version 5 adds the valid-ERD
  // fingerprint.
  static constexpr uint32_t kRecordVersion = 5;

  // Grace period between a successful publish and the reboot, so the last
  // discovery packets leave through MQTT before the reset.
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

  // How many reboots an aborted discovery run may arm before the machine gives
  // up and stays up. A freed run allocation does not re-coalesce (see
  // abort_publish_run_()), and a failed run never saves the record, so the next
  // boot retries -- without a bound a permanently failing payload (a corrupt
  // discovery data blob, say) would reboot on every boot forever.
  static constexpr uint8_t kMaxAbortReboots = 3;

  // Why the current phase was entered. Orthogonal to the phase: the same
  // cleanup -> publish -> reboot sequence runs for every trigger, and a few
  // transitions depend on which one started it.
  enum Trigger : uint8_t {
    kTriggerNone,
    kTriggerOta,                // stale topics from a previous layout must go
    kTriggerDiscoveryRefresh,   // user pressed the Discovery Refresh button
    kTriggerInitial,            // fresh install: publish, no cleanup
    kTriggerPendingRepublish,   // a teardown in an earlier boot never got its republish
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
    // Reads how many discovery-abort reboots have already been armed (0 when
    // nothing is stored). Persisted because the reboot itself erases the only
    // in-RAM evidence that the run failed.
    uint8_t (*read_abort_reboot_count)(void* context);
    // Persists that count; false when the write failed, which makes the abort
    // path stay up instead of rebooting -- an unbounded retry is a reboot loop
    // with no way to stop it.
    bool (*write_abort_reboot_count)(void* context, uint8_t count);
    // Reads whether a cleanup tore down the broker's retained discovery configs
    // and the republish that should have followed never completed (issue #203).
    // Set when a cleanup starts and cleared when a publish run records its
    // result, so a boot that finds it republishes even though the stored record
    // still matches -- which is exactly the state a torn-down refresh leaves
    // behind: the record from the last successful publish is unchanged, while
    // Home Assistant has no configs left to publish against.
    bool (*read_publish_pending)(void* context);
    // Persists that marker; false when the write failed. A cleanup that cannot
    // record its intent refuses to remove anything: without the marker a failed
    // republish is not recoverable on the next boot, so tearing the topics down
    // would be an unrecoverable action.
    bool (*write_publish_pending)(void* context, bool pending);
    // Largest contiguous free heap block in bytes. Freeing a run's ~52 KB does
    // not give the heap back to the next allocation (see abort_publish_run_()),
    // so begin_publish_() refuses to start a run whose buffers cannot fit and
    // lets the bounded abort reboot recover the heap instead.
    uint32_t (*read_largest_free_block)(void* context);
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

  // Main-loop-only: reads the plain initial_publish_done_ latch before the
  // claim, so it must not run beside loop() on another core.
  void trigger_initial_discovery();

  // Main-loop-only: compares the persisted record with the current discovery
  // inputs and queues the matching action: initial publish when nothing is
  // stored, cleanup + republish when the version, hash, device ID or flags
  // changed.
  void check_discovery_changes(const char* current_device_id);

  // Fingerprint of what discovery is filtered by right now: the host board
  // and the valid-ERD set from the appliance's feature bits. The bridge sets
  // it before check_discovery_changes(); it is stored with each publish, and a
  // different value at a later check means the published entity list no
  // longer matches the appliance (another board, new firmware with other
  // features), so discovery is cleaned up and republished. 0 means there is
  // no filter or it is unknown (feature bits unreadable, appliance API
  // parsing off): such a check never triggers a republish, so a transient
  // feature-bit failure at boot cannot cause a republish loop.
  void set_valid_erds_fingerprint(uint32_t fingerprint) { this->valid_erds_fingerprint_ = fingerprint; }
  uint32_t valid_erds_fingerprint() const { return this->valid_erds_fingerprint_; }

  // Bridge readiness plus a completed device identity.
  bool is_ready() const;

  Phase phase() const { return static_cast<Phase>(phase_.load(std::memory_order_acquire)); }
  // Main-loop-only diagnostic read: unlike phase(), this plain read has no
  // acquire edge of its own. Call it only from the main loop, or — as the
  // concurrency test does — only while phase() reports a queued or active
  // phase, whose acquire load orders the claimer's trigger_ write.
  Trigger trigger() const { return trigger_; }
  bool initial_publish_done() const { return initial_publish_done_; }

  static const char* phase_name(Phase phase);
  const char* phase_name() const { return phase_name(phase()); }

 private:
  // Atomically claims the machine from Idle: on success trigger_ is set and
  // the phase is published as target; on failure logs why and returns false.
  bool claim_request_(Phase target, Trigger trigger, const char* request);
  // Queues the cleanup + republish that recovers a teardown left un-republished
  // by an earlier boot (see read_publish_pending). Main-loop-only, like the
  // public triggers: it is reached only from check_discovery_changes().
  void trigger_pending_republish_();
  void set_phase_(Phase phase);
  // Configures and starts the topic cleanup; stays QueuedCleanup if not ready.
  void begin_cleanup_();
  // Resets, configures and starts a discovery run.
  void begin_publish_();
  // Drives a run to completion: a successful run records the outcome and arms
  // the reboot, a failed one goes through abort_publish_run_().
  void finish_publish_();
  // Releases the run's allocations without saving a record, so the next boot's
  // change check retries, then arms a reboot while the kMaxAbortReboots budget
  // lasts. Freeing the ~52 KB of run allocations does not restore the heap:
  // measured on hardware, the largest free block after an aborted run stays at
  // 102 400-143 360 B for the rest of the boot against 237 568 B after a clean
  // one, because the region is bounded by other survivors while the publisher
  // churns. Only a reset re-coalesces it. The caller has already confirmed the
  // firmware and logged why.
  void abort_publish_run_();

  ha_discovery_manager_t* ha_discovery_manager_{nullptr};
  DeviceIdentityManager* device_identity_manager_{nullptr};
  esphome_mqtt_client_adapter_t* mqtt_client_adapter_{nullptr};
  erd_cache_t* erd_cache_{nullptr};
  bool generate_device_config_{false};
  bool appliance_api_parsing_{false};
  bool filter_config_topics_{false};
  uint32_t valid_erds_fingerprint_{0};
  // valid_erds_republishes for the record the next publish saves
  uint8_t next_valid_erds_republishes_{0};

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
  // When Cleaning/Publishing was entered; seeds progress_ms_ at phase entry
  // and is not read anywhere else -- both in-flight phases time out from
  // progress_ms_.
  uint32_t phase_start_ms_{0};
  // Last loop() that saw the active phase do work -- a topic removed while
  // Cleaning, an entity published while Publishing -- and the work counter it
  // saw. kPhaseTimeoutMs is an idle timeout on this progress, so a
  // congested-but-progressing phase is never aborted and a silent one is.
  uint32_t progress_ms_{0};
  uint32_t progress_snapshot_{0};
};

}  // namespace geappliances_bridge
}  // namespace esphome
