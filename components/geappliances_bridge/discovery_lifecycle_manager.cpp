#include "discovery_lifecycle_manager.h"

#include <cstring>
#include <inttypes.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "geappliances_bridge_log.h"

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

#if defined(USE_ESP_IDF_STUBS)
// Unit-test harness: NVS, the OTA rollback API, the task watchdog and the
// ESPHome MQTT availability object are not available, so the hook defaults
// below are no-ops and tests inject their own recorders.
#else
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esphome/core/application.h"
#include "esphome/core/preferences.h"
#include "esphome/components/mqtt/mqtt_client.h"
#endif

GEA_TAG(TAG) = "discovery_lifecycle_manager";

namespace esphome {
namespace geappliances_bridge {

namespace {


#if defined(USE_ESP_IDF_STUBS)

void apply_mqtt_availability(void*, ha_discovery_manager_t*) {}

bool load_discovery_record(void*, DiscoveryLifecycleManager::DiscoveryRecord*) { return false; }

void save_discovery_record(void*, const DiscoveryLifecycleManager::DiscoveryRecord&) {}

void confirm_firmware(void*) {}

// "No abort reboot armed yet", and the write always succeeds: the same view a
// working NVS gives at count 0. Tests that need the spent budget or a failing
// write inject their own recorder.
uint8_t read_abort_reboot_count(void*) { return 0; }

bool write_abort_reboot_count(void*, uint8_t) { return true; }

void feed_watchdog(void*) {}

void reboot(void*) {}

#else
// NVS key for the persisted DiscoveryRecord ("disc").
constexpr uint32_t kDiscoveryNvsKey = 0x64697363u;
// ESPHome's own safe-mode counter; zeroing it marks the running firmware good.
constexpr uint32_t kSafeModeRtcKey = 233825507UL;
// NVS key for the discovery-abort reboot budget ("drab"). Separate from the
// record and from ESPHome's safe-mode counter: those are firmware-validation
// state, this counts discovery runs.
constexpr uint32_t kAbortRebootNvsKey = 0x64726162u;

void apply_mqtt_availability(void*, ha_discovery_manager_t* manager) {
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr) return;
  const auto& availability = mqtt_client->get_availability();
  // Reuse ESPHome's own birth/LWT availability (default "<topic_prefix>/status")
  // so HA marks appliance entities unavailable when the bridge drops offline.
  // An empty topic (birth/will disabled) leaves availability off.
  ha_discovery_manager_set_availability(manager,
      availability.topic.c_str(),
      availability.payload_available.c_str(),
      availability.payload_not_available.c_str());
}

bool load_discovery_record(void*, DiscoveryLifecycleManager::DiscoveryRecord* out) {
  if (out == nullptr) return false;
  auto pref = global_preferences->make_preference<DiscoveryLifecycleManager::DiscoveryRecord>(
      kDiscoveryNvsKey);
  DiscoveryLifecycleManager::DiscoveryRecord stored{};
  if (pref.load(&stored)) {
    return DiscoveryLifecycleManager::decode_record(
        reinterpret_cast<const uint8_t*>(&stored), sizeof(stored), out);
  }
  // A record written by firmware with the version 4 layout: ESPHome won't load
  // a blob of another size as the current struct, so read the old size.
  auto legacy = global_preferences->make_preference<DiscoveryLifecycleManager::DiscoveryRecordV4>(
      kDiscoveryNvsKey);
  DiscoveryLifecycleManager::DiscoveryRecordV4 old{};
  if (!legacy.load(&old)) return false;
  return DiscoveryLifecycleManager::decode_record(old.bytes, sizeof(old.bytes), out);
}

void save_discovery_record(void*, const DiscoveryLifecycleManager::DiscoveryRecord& record) {
  auto pref = global_preferences->make_preference<DiscoveryLifecycleManager::DiscoveryRecord>(
      kDiscoveryNvsKey);
  pref.save(&record);
  global_preferences->sync();
  ESP_LOGD(TAG, "Stored discovery state hash=0x%08" PRIx32
           " device_id=%s api_parsing=%d filter_topics=%d valid_erds=0x%08" PRIx32,
           record.hash, record.device_id,
           record.appliance_api_parsing, record.filter_config_topics,
           record.valid_erds_fingerprint);
}

void confirm_firmware(void*) {
  // Always confirm the firmware is valid so a good OTA is not rolled back,
  // regardless of the discovery outcome.
  uint32_t val = 0;
  ESPPreferenceObject rtc_pref =
      global_preferences->make_preference<uint32_t>(kSafeModeRtcKey, false);
  rtc_pref.save(&val);
  global_preferences->sync();
  esp_ota_mark_app_valid_cancel_rollback();
  ESP_LOGI(TAG, "Safe mode counter cleared, OTA rollback cancelled");
}

uint8_t read_abort_reboot_count(void*) {
  auto pref = global_preferences->make_preference<uint8_t>(kAbortRebootNvsKey);
  uint8_t count = 0;
  if (!pref.load(&count)) return 0;
  return count;
}

bool write_abort_reboot_count(void*, uint8_t count) {
  auto pref = global_preferences->make_preference<uint8_t>(kAbortRebootNvsKey);
  if (!pref.save(&count)) {
    ESP_LOGE(TAG, "Could not persist the discovery-abort reboot count");
    return false;
  }
  global_preferences->sync();
  return true;
}

void feed_watchdog(void*) { esp_task_wdt_reset(); }

void reboot(void*) { esphome::App.safe_reboot(); }

#endif

}  // namespace

void DiscoveryLifecycleManager::init(
    ha_discovery_manager_t* ha_discovery_manager,
    DeviceIdentityManager& device_identity_manager,
    esphome_mqtt_client_adapter_t& mqtt_client_adapter,
    erd_cache_t* erd_cache,
    bool generate_device_config,
    bool appliance_api_parsing,
    bool filter_config_topics,
    bool (*is_ready)(void* context),
    void* is_ready_context,
    const Hooks& hooks)
{
  this->ha_discovery_manager_ = ha_discovery_manager;
  this->device_identity_manager_ = &device_identity_manager;
  this->mqtt_client_adapter_ = &mqtt_client_adapter;
  this->erd_cache_ = erd_cache;
  this->generate_device_config_ = generate_device_config;
  this->appliance_api_parsing_ = appliance_api_parsing;
  this->filter_config_topics_ = filter_config_topics;
  this->is_ready_ = is_ready;
  this->is_ready_context_ = is_ready_context;

  this->hooks_ = hooks;
  if (this->hooks_.apply_mqtt_availability == nullptr) {
    this->hooks_.apply_mqtt_availability = &apply_mqtt_availability;
  }
  if (this->hooks_.load_discovery_record == nullptr) {
    this->hooks_.load_discovery_record = &load_discovery_record;
  }
  if (this->hooks_.save_discovery_record == nullptr) {
    this->hooks_.save_discovery_record = &save_discovery_record;
  }
  if (this->hooks_.confirm_firmware == nullptr) {
    this->hooks_.confirm_firmware = &confirm_firmware;
  }
  if (this->hooks_.read_abort_reboot_count == nullptr) {
    this->hooks_.read_abort_reboot_count = &read_abort_reboot_count;
  }
  if (this->hooks_.write_abort_reboot_count == nullptr) {
    this->hooks_.write_abort_reboot_count = &write_abort_reboot_count;
  }
  if (this->hooks_.feed_watchdog == nullptr) {
    this->hooks_.feed_watchdog = &feed_watchdog;
  }
  if (this->hooks_.reboot == nullptr) {
    this->hooks_.reboot = &reboot;
  }
  this->get_time_ms_ = &esphome::millis;

  this->set_phase_(kPhaseIdle);
  this->trigger_ = kTriggerNone;
  this->initial_publish_done_ = false;
  this->reboot_start_ms_ = 0;
  this->phase_start_ms_ = 0;
  this->progress_ms_ = 0;
  this->progress_snapshot_ = 0;
}

const char* DiscoveryLifecycleManager::phase_name(Phase phase) {
  switch (phase) {
    case kPhaseIdle: return "idle";
    case kPhaseQueuedInitial: return "initial publish queued";
    case kPhaseQueuedCleanup: return "cleanup queued";
    case kPhaseCleaning: return "cleaning topics";
    case kPhasePublishing: return "publishing";
    case kPhaseRebootPending: return "reboot pending";
    case kPhaseClaimed: return "request in progress";
  }
  return "unknown";
}

bool DiscoveryLifecycleManager::is_ready() const {
  return this->is_ready_ != nullptr &&
         this->is_ready_(this->is_ready_context_) &&
         this->device_identity_manager_ != nullptr &&
         this->device_identity_manager_->get_state() == DEVICE_ID_STATE_COMPLETE;
}

void DiscoveryLifecycleManager::set_phase_(Phase phase)
{
  this->phase_.store(static_cast<uint8_t>(phase), std::memory_order_release);
}

bool DiscoveryLifecycleManager::claim_request_(Phase target, Trigger trigger,
                                               [[maybe_unused]] const char* request)
{
  // Accept only from Idle, atomically: trigger_discovery_refresh() can run
  // off the main loop, so a plain check-then-act could let two triggers both
  // pass the Idle check and overwrite each other's trigger. The claim lands
  // in kPhaseClaimed first so loop() never observes a queued phase whose
  // trigger_ is still stale; the release store publishes trigger_ with it.
  uint8_t expected = static_cast<uint8_t>(kPhaseIdle);
  if (!this->phase_.compare_exchange_strong(expected, static_cast<uint8_t>(kPhaseClaimed),
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
    ESP_LOGW(TAG, "%s ignored: discovery is %s", request,
             phase_name(static_cast<Phase>(expected)));
    return false;
  }
  this->trigger_ = trigger;
  this->set_phase_(target);
  return true;
}

bool DiscoveryLifecycleManager::decode_record(const uint8_t* blob, size_t size,
                                              DiscoveryRecord* out) {
  if (blob == nullptr || out == nullptr) return false;
  if (size != sizeof(DiscoveryRecord) && size != kRecordV4Size) return false;
  DiscoveryRecord record{};
  memcpy(&record, blob, size);  // v4 is a prefix; the newer fields stay 0
  *out = record;
  return true;
}

void DiscoveryLifecycleManager::trigger_ota_cleanup() {
  if (!this->claim_request_(kPhaseQueuedCleanup, kTriggerOta, "Discovery cleanup")) {
    return;
  }
  ESP_LOGI(TAG, "OTA discovery cleanup queued, will execute when appliance is ready");
}

void DiscoveryLifecycleManager::trigger_discovery_refresh() {
  if (!this->claim_request_(kPhaseQueuedCleanup, kTriggerDiscoveryRefresh, "Discovery refresh")) {
    return;
  }
  ESP_LOGI(TAG, "Discovery refresh queued, will execute when appliance is ready");
}

void DiscoveryLifecycleManager::trigger_initial_discovery() {
  if (this->initial_publish_done_) return;
  if (!this->claim_request_(kPhaseQueuedInitial, kTriggerInitial, "Initial discovery")) {
    return;
  }
  ESP_LOGI(TAG, "Initial HA discovery publish queued, will execute when appliance is ready");
}

void DiscoveryLifecycleManager::check_discovery_changes(const char* current_device_id) {
  if (current_device_id == nullptr) {
    return;
  }
  const Phase current = this->phase();
  if (current != kPhaseIdle) {
    ESP_LOGW(TAG, "Discovery change check skipped: discovery is %s", phase_name(current));
    return;
  }
  if (!this->generate_device_config_) {
    return;
  }
  this->next_valid_erds_republishes_ = 0;

  // Refresh the availability topic before hashing: the manager's buffers are
  // only guaranteed populated at publish time, and the hash must reflect the
  // current LWT topic so a topic_prefix/node-name change triggers a republish.
  this->hooks_.apply_mqtt_availability(this->hooks_.context, this->ha_discovery_manager_);

  DiscoveryRecord stored{};
  if (!this->hooks_.load_discovery_record(this->hooks_.context, &stored)) {
    this->trigger_initial_discovery();
    return;
  }

  // Older records have no compatible combined custom hash/config state.
  if (stored.version != kRecordVersion) {
    ESP_LOGI(TAG, "Discovery hash version changed; cleaning old topics");
    this->trigger_ota_cleanup();
    return;
  }

  const bool hash_changed =
      (stored.hash != ha_discovery_manager_data_hash(this->ha_discovery_manager_));
  const bool device_id_changed =
      (stored.device_id[0] != '\0' && strcmp(stored.device_id, current_device_id) != 0);
  const bool api_parsing_changed =
      (stored.appliance_api_parsing != this->appliance_api_parsing_);
  const bool filter_topics_changed =
      (stored.filter_config_topics != this->filter_config_topics_);
  // Only a known fingerprint counts (see set_valid_erds_fingerprint())
  const bool valid_erds_changed =
      (this->valid_erds_fingerprint_ != 0 &&
       stored.valid_erds_fingerprint != this->valid_erds_fingerprint_);

  if (hash_changed || device_id_changed || api_parsing_changed || filter_topics_changed) {
    ESP_LOGI(TAG, "Discovery state changed (hash=%s, device_id=%s, api_parsing=%s, filter_topics=%s, valid_erds=%s), cleaning old topics",
             hash_changed ? "changed" : "same",
             device_id_changed ? "changed" : "same",
             api_parsing_changed ? "changed" : "same",
             filter_topics_changed ? "changed" : "same",
             valid_erds_changed ? "changed" : "same");
    this->trigger_ota_cleanup();
    return;
  }

  if (valid_erds_changed) {
    // The only trigger read live from the appliance: bound it (see
    // kMaxValidErdsRepublishes)
    if (stored.valid_erds_republishes >= kMaxValidErdsRepublishes) {
      ESP_LOGW(TAG, "Valid-ERD set changed again after %u republishes in a row; not republishing "
                    "(the appliance's feature bits may be read inconsistently). Press Discovery "
                    "Refresh to republish.",
               (unsigned) stored.valid_erds_republishes);
      if (stored.valid_erds_stable_checks != 0) {
        stored.valid_erds_stable_checks = 0;  // the streak is broken
        this->hooks_.save_discovery_record(this->hooks_.context, stored);
      }
      return;
    }
    this->next_valid_erds_republishes_ = static_cast<uint8_t>(stored.valid_erds_republishes + 1);
    ESP_LOGI(TAG, "Discovery state changed (valid_erds=changed), cleaning old topics");
    this->trigger_ota_cleanup();
    return;
  }

  // Nothing changed. Enough checks in a row with the fingerprint unchanged
  // end a run of fingerprint-driven republishes.
  if (stored.valid_erds_republishes != 0 && this->valid_erds_fingerprint_ != 0) {
    stored.valid_erds_stable_checks++;
    if (stored.valid_erds_stable_checks >= kValidErdsStableChecksToReset) {
      stored.valid_erds_republishes = 0;
      stored.valid_erds_stable_checks = 0;
    }
    this->hooks_.save_discovery_record(this->hooks_.context, stored);
  }
}

void DiscoveryLifecycleManager::begin_cleanup_() {
  // Unlike the automatic initial publish, an explicit cleanup trigger (an OTA
  // version change or the Discovery Refresh button) intentionally proceeds
  // even when generate_device_config_ is off: stale topics from an earlier
  // firmware still need removing.
  if (!is_ready()) {
    return;
  }
  ha_discovery_cleanup_configure(&this->ha_discovery_manager_->cleanup,
      this->device_identity_manager_->get_device_id(),
      &this->mqtt_client_adapter_->interface, this->get_time_ms_);
  ha_discovery_cleanup_start(&this->ha_discovery_manager_->cleanup);
  if (this->trigger_ == kTriggerDiscoveryRefresh) {
    ESP_LOGI(TAG, "Starting discovery-refresh HA discovery cleanup...");
  } else {
    ESP_LOGI(TAG, "Starting OTA-triggered HA discovery cleanup...");
  }
  this->phase_start_ms_ = this->get_time_ms_();
  // Idle timeout: measures silence from phase entry until the module removes
  // its first topic. The snapshot is seeded from the module instead of assumed
  // zero, so a re-run can never inherit a stale count.
  this->progress_ms_ = this->phase_start_ms_;
  this->progress_snapshot_ =
    ha_discovery_cleanup_total_removed(&this->ha_discovery_manager_->cleanup);
  this->set_phase_(kPhaseCleaning);
}

void DiscoveryLifecycleManager::begin_publish_() {
  ha_discovery_manager_init(this->ha_discovery_manager_);

  ha_discovery_manager_configure(
      this->ha_discovery_manager_,
      this->device_identity_manager_->get_device_id(),
      this->device_identity_manager_->get_model_number(),
      this->device_identity_manager_->get_serial_number(),
      this->device_identity_manager_->get_appliance_type(),
      this->filter_config_topics_,
      this->erd_cache_,
      &this->mqtt_client_adapter_->interface);
  this->hooks_.apply_mqtt_availability(this->hooks_.context, this->ha_discovery_manager_);
  ha_discovery_manager_start(this->ha_discovery_manager_);
  this->phase_start_ms_ = this->get_time_ms_();
  // Idle timeout: measures silence from phase entry until the first entity is
  // published. Seeded from the run, which init() above just zeroed.
  this->progress_ms_ = this->phase_start_ms_;
  this->progress_snapshot_ =
    ha_discovery_manager_total_published(this->ha_discovery_manager_);
  this->set_phase_(kPhasePublishing);
}

void DiscoveryLifecycleManager::finish_publish_() {
  // A discovery run ends in either COMPLETE or FAILED; is_processing() is
  // false for both, so branch on the actual state. A FAILED run (e.g. a
  // buffer-allocation or decompression error) must not be recorded as a
  // successful publish, or the next boot's change check sees a matching
  // hash and never retries.
  const bool discovery_failed =
      ha_discovery_manager_get_state(this->ha_discovery_manager_) == ha_discovery_state_failed;

  this->hooks_.confirm_firmware(this->hooks_.context);

  if (discovery_failed) {
    // abort_publish_run_() releases the discovery allocations (the ~52 KB of
    // run buffers plus tinfl state), clears run state and decides whether to
    // reboot. It never saves the record: leaving the stored record
    // stale/absent is what makes check_discovery_changes() re-trigger
    // discovery on the next boot.
    this->abort_publish_run_();
    ESP_LOGE(TAG, "HA discovery failed; not saving discovery state, will retry on next boot");
    return;
  }

  if (this->trigger_ == kTriggerInitial) {
    this->initial_publish_done_ = true;
  }
  [[maybe_unused]] const char* trigger_kind = this->trigger_ == kTriggerInitial
      ? "Initial"
      : (this->trigger_ == kTriggerDiscoveryRefresh ? "Refresh" : "OTA");
  // A Discovery Refresh run ends any run of fingerprint-driven republishes.
  // Decided from trigger_ (published by the claim's release store), not by
  // the refresh trigger writing the count: that may run on another task.
  const bool refresh_run = this->trigger_ == kTriggerDiscoveryRefresh;
  this->trigger_ = kTriggerNone;

  DiscoveryRecord record{};
  record.version = kRecordVersion;
  record.hash = ha_discovery_manager_data_hash(this->ha_discovery_manager_);
  strncpy(record.device_id,
          this->device_identity_manager_->get_device_id(),
          sizeof(record.device_id) - 1);
  record.device_id[sizeof(record.device_id) - 1] = '\0';
  record.appliance_api_parsing = this->appliance_api_parsing_;
  record.filter_config_topics = this->filter_config_topics_;
  record.valid_erds_fingerprint = this->valid_erds_fingerprint_;
  record.valid_erds_republishes = refresh_run ? 0 : this->next_valid_erds_republishes_;
  record.valid_erds_stable_checks = 0;
  this->next_valid_erds_republishes_ = 0;
  this->hooks_.save_discovery_record(this->hooks_.context, record);

  // The reboot below pays for whatever the run did to the heap, so the abort
  // budget is spent for a different reason now; clearing it gives a later
  // transient failure its full set of attempts.
  if (this->hooks_.read_abort_reboot_count(this->hooks_.context) != 0) {
    this->hooks_.write_abort_reboot_count(this->hooks_.context, 0);
  }

  ESP_LOGI(TAG, "%s HA discovery publish complete, preparing reboot...", trigger_kind);

  this->set_phase_(kPhaseRebootPending);
  this->reboot_start_ms_ = this->get_time_ms_();
}
// Failed-run contract shared by the publish-timeout abort and a FAILED run: the
// run's allocations are released and no record is saved, so the next boot's
// change check retries. The caller has already confirmed the firmware (it is
// confirmed on success too) and logged the reason.
//
// Releasing the ~52 KB does not release the fragmentation it caused. Measured on
// hardware, the largest free block after an aborted run stays at 102 400-143 360
// B for the rest of the boot against 237 568 B after a clean one: the region is
// bounded by other survivors while the publisher churns, so nothing re-coalesces
// it short of a reset. Hence the reboot -- bounded, because a failed run never
// saves the record and the next boot retries, so a permanently failing payload
// would otherwise reboot on every boot forever.
void DiscoveryLifecycleManager::abort_publish_run_() {
  ha_discovery_manager_init(this->ha_discovery_manager_);
  this->trigger_ = kTriggerNone;

  const uint8_t armed = this->hooks_.read_abort_reboot_count(this->hooks_.context);
  if (armed >= kMaxAbortReboots) {
    ESP_LOGE(TAG, "HA discovery aborted and the reboot budget (%" PRIu8
             ") is spent, staying up. Discovery Refresh or a power cycle will retry.",
             static_cast<uint8_t>(kMaxAbortReboots));
    this->set_phase_(kPhaseIdle);
    return;
  }
  if (!this->hooks_.write_abort_reboot_count(this->hooks_.context,
                                             static_cast<uint8_t>(armed + 1))) {
    // Rebooting without remembering that we rebooted is an unbounded retry
    // loop with nothing to stop it, so an unwritable counter stops recovery.
    ESP_LOGE(TAG, "HA discovery aborted but the reboot count could not be stored; not rebooting");
    this->set_phase_(kPhaseIdle);
    return;
  }
  ESP_LOGW(TAG, "HA discovery aborted; rebooting to recover the heap (attempt %" PRIu8
           " of %" PRIu8 ")",
           static_cast<uint8_t>(armed + 1), static_cast<uint8_t>(kMaxAbortReboots));
  this->set_phase_(kPhaseRebootPending);
  this->reboot_start_ms_ = this->get_time_ms_();
}

void DiscoveryLifecycleManager::loop() {
  switch (this->phase()) {
    case kPhaseIdle:
      return;

    case kPhaseClaimed:
      // A trigger is between the claim and publishing its queued phase; the
      // next iteration will see QueuedInitial/QueuedCleanup.
      return;

    case kPhaseQueuedInitial:
      if (!this->generate_device_config_ || !is_ready()) {
        return;
      }
      ESP_LOGI(TAG, "Starting initial HA discovery publish...");
      this->begin_publish_();
      return;

    case kPhaseQueuedCleanup:
      this->begin_cleanup_();
      return;

    case kPhaseCleaning: {
      // Idle timeout on topics removed: a congested broker makes removal
      // publishes drop and retry through the cleanup module's backoff, one
      // topic per main-loop iteration, so a large cleanup can exceed any fixed
      // entry-based bound while still working. Only a run that stops removing
      // is wedged (e.g. the broker connection dropped mid-run).
      const uint32_t removed =
        ha_discovery_cleanup_total_removed(&this->ha_discovery_manager_->cleanup);
      if (removed != this->progress_snapshot_) {
        this->progress_snapshot_ = removed;
        this->progress_ms_ = this->get_time_ms_();
      }
      if (this->get_time_ms_() - this->progress_ms_ >= kPhaseTimeoutMs) {
        // Aborting is the only way the machine can accept another request in
        // this boot; the record was never saved, so the next boot's change
        // check retries too.
        ESP_LOGE(TAG, "HA discovery cleanup made no removal progress for %" PRIu32
                 " ms; aborting. Discovery Refresh or a reboot will retry.",
                 kPhaseTimeoutMs);
        ha_discovery_cleanup_destroy(&this->ha_discovery_manager_->cleanup);
        this->trigger_ = kTriggerNone;
        this->set_phase_(kPhaseIdle);
        return;
      }
      ha_discovery_cleanup_run(&this->ha_discovery_manager_->cleanup);
      if (!ha_discovery_cleanup_is_done(&this->ha_discovery_manager_->cleanup)) {
        return;
      }
      ESP_LOGI(TAG, "HA discovery cleanup complete, publishing fresh discovery...");
      ha_discovery_cleanup_destroy(&this->ha_discovery_manager_->cleanup);
      this->begin_publish_();
      return;
    }

    case kPhasePublishing:
      if (ha_discovery_manager_is_processing(this->ha_discovery_manager_)) {
        // Idle timeout, not a wall-clock bound on the run: a congested broker
        // makes entities drop and retry through the manager's publish backoff,
        // and the largest runs can exceed any fixed entry-based bound while
        // still publishing. Only a run that stops publishing is wedged.
        const uint32_t published =
          ha_discovery_manager_total_published(this->ha_discovery_manager_);
        if (published != this->progress_snapshot_) {
          this->progress_snapshot_ = published;
          this->progress_ms_ = this->get_time_ms_();
        }
        if (this->get_time_ms_() - this->progress_ms_ >= kPhaseTimeoutMs) {
          ESP_LOGE(TAG, "HA discovery publish made no progress for %" PRIu32
                   " ms; aborting. Discovery Refresh or a reboot will retry.",
                   kPhaseTimeoutMs);
          // Same contract as the failed-run path: the firmware is confirmed
          // regardless of the discovery outcome, the run's allocations are
          // released, and no record is saved so the next boot retries.
          this->hooks_.confirm_firmware(this->hooks_.context);
          this->abort_publish_run_();
          return;
        }
        ha_discovery_manager_run(this->ha_discovery_manager_);
        return;
      }
      this->finish_publish_();
      return;

    case kPhaseRebootPending: {
      this->hooks_.feed_watchdog(this->hooks_.context);
      const uint32_t elapsed = this->get_time_ms_() - this->reboot_start_ms_;
      if (elapsed < kRebootDelayMs) {
        return;
      }
      ESP_LOGI(TAG, "Rebooting after discovery cleanup + republish...");
      this->set_phase_(kPhaseIdle);
      this->hooks_.reboot(this->hooks_.context);
      return;
    }
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
