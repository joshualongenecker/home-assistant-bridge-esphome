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

// NVS key for the persisted DiscoveryRecord ("disc").
constexpr uint32_t kDiscoveryNvsKey = 0x64697363u;
// ESPHome's own safe-mode counter; zeroing it marks the running firmware good.
constexpr uint32_t kSafeModeRtcKey = 233825507UL;

#if defined(USE_ESP_IDF_STUBS)

void apply_mqtt_availability(void*, ha_discovery_manager_t*) {}

bool load_discovery_record(void*, DiscoveryLifecycleManager::DiscoveryRecord*) { return false; }

void save_discovery_record(void*, const DiscoveryLifecycleManager::DiscoveryRecord&) {}

void confirm_firmware(void*) {}

void feed_watchdog(void*) {}

void reboot(void*) {}

#else

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
  if (!pref.load(&stored)) return false;
  *out = stored;
  return true;
}

void save_discovery_record(void*, const DiscoveryLifecycleManager::DiscoveryRecord& record) {
  auto pref = global_preferences->make_preference<DiscoveryLifecycleManager::DiscoveryRecord>(
      kDiscoveryNvsKey);
  pref.save(&record);
  global_preferences->sync();
  ESP_LOGD(TAG, "Stored discovery state hash=0x%08" PRIx32
           " device_id=%s api_parsing=%d filter_topics=%d",
           record.hash, record.device_id,
           record.appliance_api_parsing, record.filter_config_topics);
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

void DiscoveryLifecycleManager::trigger_ota_cleanup() {
  this->claim_request_(kPhaseQueuedCleanup, kTriggerOta, "Discovery cleanup");
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
  if (this->phase() != kPhaseIdle) {
    ESP_LOGW(TAG, "Discovery change check skipped: discovery is %s", phase_name(this->phase()));
    return;
  }
  if (!this->generate_device_config_) {
    return;
  }

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

  if (hash_changed || device_id_changed || api_parsing_changed || filter_topics_changed) {
    ESP_LOGI(TAG, "Discovery state changed (hash=%s, device_id=%s, api_parsing=%s, filter_topics=%s), cleaning old topics",
             hash_changed ? "changed" : "same",
             device_id_changed ? "changed" : "same",
             api_parsing_changed ? "changed" : "same",
             filter_topics_changed ? "changed" : "same");
    this->trigger_ota_cleanup();
  }
}

void DiscoveryLifecycleManager::begin_cleanup_() {
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
    // Reset the manager to idle: this frees the discovery allocations (so the
    // ~52 KB of run allocations, buffers plus tinfl state, do not linger for
    // the rest of the boot) and clears run state.
    // Also do not save the discovery record and do not arm the reboot:
    // leaving the stored record stale/absent is what makes
    // check_discovery_changes() re-trigger discovery on the next boot.
    ha_discovery_manager_init(this->ha_discovery_manager_);
    this->trigger_ = kTriggerNone;
    this->set_phase_(kPhaseIdle);
    ESP_LOGE(TAG, "HA discovery failed; not saving discovery state, will retry on next boot");
    return;
  }

  if (this->trigger_ == kTriggerInitial) {
    this->initial_publish_done_ = true;
  }
  [[maybe_unused]] const char* trigger_kind = this->trigger_ == kTriggerInitial
      ? "Initial"
      : (this->trigger_ == kTriggerDiscoveryRefresh ? "Refresh" : "OTA");
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
  this->hooks_.save_discovery_record(this->hooks_.context, record);

  ESP_LOGI(TAG, "%s HA discovery publish complete, preparing reboot...", trigger_kind);

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
      if (this->get_time_ms_() - this->phase_start_ms_ >= kPhaseTimeoutMs) {
        // The cleanup module never settled (e.g. the broker connection
        // dropped mid-run). Aborting is the only way the machine can accept
        // another request in this boot; the record was never saved, so the
        // next boot's change check retries too.
        ESP_LOGE(TAG, "HA discovery cleanup did not finish within %" PRIu32
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
        if (this->get_time_ms_() - this->phase_start_ms_ >= kPhaseTimeoutMs) {
          ESP_LOGE(TAG, "HA discovery publish did not finish within %" PRIu32
                   " ms; aborting. Discovery Refresh or a reboot will retry.",
                   kPhaseTimeoutMs);
          // Same contract as the failed-run path: the firmware is confirmed
          // regardless of the discovery outcome, the run's allocations are
          // released, and no record is saved so the next boot retries.
          this->hooks_.confirm_firmware(this->hooks_.context);
          ha_discovery_manager_init(this->ha_discovery_manager_);
          this->trigger_ = kTriggerNone;
          this->set_phase_(kPhaseIdle);
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
