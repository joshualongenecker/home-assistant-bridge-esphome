#include "ota_cleanup_manager.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome_time_source.h"
#include "geappliances_bridge_log.h"
#include <inttypes.h>
#include "ha_discovery_data.h"

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

#if defined(USE_ESP_IDF) && !defined(USE_ESP_IDF_STUBS)
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esphome/core/preferences.h"
#elif defined(USE_ESP_IDF_STUBS)
#include "esp-idf/esp_task_wdt.h"
#endif

GEA_TAG(TAG) = "ota_cleanup_manager";

namespace esphome {
namespace geappliances_bridge {

void OtaCleanupManager::init(
    ha_discovery_manager_t* ha_discovery_manager,
    DeviceIdentityManager& device_identity_manager,
    esphome_mqtt_client_adapter_t& mqtt_client_adapter,
    erd_cache_t* erd_cache,
    bool generate_device_config,
    bool appliance_api_parsing,
    bool filter_config_topics,
    bool& steady_state_reached,
    bool& mqtt_initialized,
    std::function<void()> reboot_callback)
{
  this->ha_discovery_manager_ = ha_discovery_manager;
  this->device_identity_manager_ = &device_identity_manager;
  this->mqtt_client_adapter_ = &mqtt_client_adapter;
  this->erd_cache_ = erd_cache;
  this->generate_device_config_ = generate_device_config;
  this->appliance_api_parsing_ = appliance_api_parsing;
  this->filter_config_topics_ = filter_config_topics;
  this->steady_state_reached_ = &steady_state_reached;
  this->mqtt_initialized_ = &mqtt_initialized;
  this->reboot_callback_ = std::move(reboot_callback);
}

void OtaCleanupManager::trigger_ota_cleanup() {
  this->ota_cleanup_needed_ = true;
  this->cleanup_trigger_ = CleanupTrigger::OTA;
}

void OtaCleanupManager::trigger_discovery_refresh() {
  if (this->discovery_refresh_in_progress_ ||
      this->ota_cleanup_needed_ ||
      this->ota_cleanup_in_progress_ ||
      this->ota_discovery_publishing_ ||
      this->ota_reboot_pending_) {
    ESP_LOGW(TAG, "Discovery refresh already in progress, ignoring");
    return;
  }
  this->discovery_refresh_in_progress_ = true;
  this->cleanup_trigger_ = CleanupTrigger::DISCOVERY_REFRESH;
  ESP_LOGI(TAG, "Discovery refresh queued, will execute when appliance is ready");
}

void OtaCleanupManager::trigger_initial_discovery() {
  if (this->initial_discovery_done_) {
    return;
  }
  if (this->ota_cleanup_needed_ ||
      this->ota_cleanup_in_progress_ ||
      this->ota_discovery_publishing_ ||
      this->ota_reboot_pending_ ||
      this->discovery_refresh_in_progress_ ||
      this->initial_discovery_needed_) {
    ESP_LOGW(TAG, "Initial discovery already queued or another operation in progress, ignoring");
    return;
  }
  this->initial_discovery_needed_ = true;
  this->cleanup_trigger_ = CleanupTrigger::INITIAL;
  ESP_LOGI(TAG, "Initial HA discovery publish queued, will execute when appliance is ready");
}

void OtaCleanupManager::check_discovery_changes(const char* current_device_id) {
  if (current_device_id == nullptr) {
    return;
  }
#if defined(USE_ESP_IDF) && !defined(USE_ESP_IDF_STUBS)
  if (this->ota_cleanup_needed_ ||
      this->ota_cleanup_in_progress_ ||
      this->ota_discovery_publishing_ ||
      this->ota_reboot_pending_ ||
      this->discovery_refresh_in_progress_ ||
      this->initial_discovery_needed_) {
    ESP_LOGW(TAG, "Discovery operation already in progress, skipping change check");
    return;
  }

  if (!this->generate_device_config_) {
    return;
  }

  static const uint32_t DISCOVERY_NVS_KEY = 0x64697363u; // "disc"
  auto pref = global_preferences->make_preference<DiscoveryNVS>(DISCOVERY_NVS_KEY);
  DiscoveryNVS stored{};

  if (!pref.load(&stored)) {
    // No stored state — fresh install, trigger initial discovery.
    this->trigger_initial_discovery();
    return;
  }

  // Compare hash, device ID, and config flags.
  bool hash_changed = (stored.hash != HA_DISCOVERY_DATA_HASH);
  bool device_id_changed = (stored.device_id[0] != '\0' &&
                            strcmp(stored.device_id, current_device_id) != 0);
  bool api_parsing_changed = (stored.appliance_api_parsing != this->appliance_api_parsing_);
  bool filter_topics_changed = (stored.filter_config_topics != this->filter_config_topics_);

  if (hash_changed || device_id_changed || api_parsing_changed || filter_topics_changed) {
    ESP_LOGI(TAG, "Discovery state changed (hash=%s, device_id=%s, api_parsing=%s, filter_topics=%s), cleaning old topics",
             hash_changed ? "changed" : "same",
             device_id_changed ? "changed" : "same",
             api_parsing_changed ? "changed" : "same",
             filter_topics_changed ? "changed" : "same");
    this->trigger_ota_cleanup();
    return;
  }

  // No changes — nothing to do.
#else
  (void)current_device_id;
#endif
}

bool OtaCleanupManager::is_ready() const {
  return this->steady_state_reached_ != nullptr &&
         *this->steady_state_reached_ &&
         this->mqtt_initialized_ != nullptr &&
         *this->mqtt_initialized_ &&
         this->device_identity_manager_ != nullptr &&
         this->device_identity_manager_->get_state() == DEVICE_ID_STATE_COMPLETE;
}

bool OtaCleanupManager::start_cleanup_()
{
  if (!this->is_ready()) {
    return false;
  }
  ha_discovery_cleanup_configure(&this->ha_discovery_manager_->cleanup,
      this->device_identity_manager_->get_device_id(),
      &this->mqtt_client_adapter_->interface, esphome::millis);
  ha_discovery_cleanup_start(&this->ha_discovery_manager_->cleanup);
  this->ota_cleanup_in_progress_ = true;
  return true;
}

void OtaCleanupManager::loop() {
#if defined(USE_ESP_IDF) && !defined(USE_ESP_IDF_STUBS)
  // ── Initial discovery publish (fresh install, no cleanup or reboot) ──────
  if (!this->ota_cleanup_in_progress_ &&
      !this->ota_discovery_publishing_ && !this->ota_reboot_pending_) {
    if (this->initial_discovery_needed_ &&
        this->generate_device_config_ &&
        this->is_ready()) {
      ESP_LOGI(TAG, "Starting initial HA discovery publish...");
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
      ha_discovery_manager_start(this->ha_discovery_manager_);
      this->ota_discovery_publishing_ = true;
      this->cleanup_trigger_ = CleanupTrigger::INITIAL;
      this->initial_discovery_needed_ = false;
    }
  }

  // ── Start cleanup (OTA or DiscoveryRefresh) ──────────────────────────────
  if (!this->ota_cleanup_in_progress_ &&
      !this->ota_discovery_publishing_ && !this->ota_reboot_pending_) {
    bool ota_trigger = this->generate_device_config_ && this->ota_cleanup_needed_;
    bool refresh_trigger = this->discovery_refresh_in_progress_;

    if (ota_trigger || refresh_trigger) {
      if (this->start_cleanup_()) {
        if (ota_trigger) {
          ESP_LOGI(TAG, "Starting OTA-triggered HA discovery cleanup...");
        }
        if (refresh_trigger) {
          this->discovery_refresh_in_progress_ = false;
        }
      }
    }
  }

  // ── Drive cleanup (shared by OTA and DiscoveryRefresh paths) ─────────────
  if (this->ota_cleanup_in_progress_) {
    ha_discovery_cleanup_run(&this->ha_discovery_manager_->cleanup);
    if (ha_discovery_cleanup_is_done(&this->ha_discovery_manager_->cleanup)) {
      this->ota_cleanup_in_progress_ = false;
      if (this->cleanup_trigger_ == CleanupTrigger::OTA) {
        this->ota_cleanup_needed_ = false;
      }
      ESP_LOGI(TAG, "HA discovery cleanup complete, publishing fresh discovery...");
      ha_discovery_cleanup_destroy(&this->ha_discovery_manager_->cleanup);
      ha_discovery_manager_init(this->ha_discovery_manager_);

      // Publish fresh HA discovery payloads.
      ha_discovery_manager_configure(
        this->ha_discovery_manager_,
        this->device_identity_manager_->get_device_id(),
        this->device_identity_manager_->get_model_number(),
        this->device_identity_manager_->get_serial_number(),
        this->device_identity_manager_->get_appliance_type(),
        this->filter_config_topics_,
        this->erd_cache_,
        &this->mqtt_client_adapter_->interface);
      ha_discovery_manager_start(this->ha_discovery_manager_);
      this->ota_discovery_publishing_ = true;
    }
  }

  // ── Drive discovery publishing ───────────────────────────────────────────
  if (this->ota_discovery_publishing_) {
    if (ha_discovery_manager_is_processing(this->ha_discovery_manager_)) {
      ha_discovery_manager_run(this->ha_discovery_manager_);
    } else {
      this->ota_discovery_publishing_ = false;

      if (this->cleanup_trigger_ == CleanupTrigger::INITIAL) {
        // Initial publish: no cleanup needed, but still reboot after.
        this->initial_discovery_done_ = true;
        this->cleanup_trigger_ = CleanupTrigger::NONE;
        ESP_LOGI(TAG, "Initial HA discovery publish complete, preparing reboot...");
      } else {
        // OTA or DiscoveryRefresh: prepare for reboot.
        this->cleanup_trigger_ = CleanupTrigger::NONE;
        ESP_LOGI(TAG, "OTA HA discovery publish complete, preparing reboot...");
      }

      // Clear safe mode counter and mark OTA valid before reboot.
      {
        uint32_t val = 0;
        static constexpr uint32_t SAFE_MODE_RTC_KEY = 233825507UL;
        ESPPreferenceObject rtc_pref = global_preferences->make_preference<uint32_t>(SAFE_MODE_RTC_KEY, false);
        rtc_pref.save(&val);
        global_preferences->sync();
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Safe mode counter cleared, OTA rollback cancelled");
      }

      this->ota_reboot_pending_ = true;
      this->ota_reboot_start_ms_ = esphome::millis();

      // Store current discovery state (hash + device ID) in NVS for
      // change detection on next boot.
      {
        static const uint32_t DISCOVERY_NVS_KEY = 0x64697363u; // "disc"
        auto pref = global_preferences->make_preference<DiscoveryNVS>(DISCOVERY_NVS_KEY);
        DiscoveryNVS state{};
        state.hash = HA_DISCOVERY_DATA_HASH;
        strncpy(state.device_id,
                this->device_identity_manager_->get_device_id(),
                sizeof(state.device_id) - 1);
        state.device_id[sizeof(state.device_id) - 1] = '\0';
        state.appliance_api_parsing = this->appliance_api_parsing_;
        state.filter_config_topics = this->filter_config_topics_;
        pref.save(&state);
        global_preferences->sync();
        ESP_LOGD(TAG, "Stored discovery state hash=0x%08" PRIx32
                 " device_id=%s api_parsing=%d filter_topics=%d",
                 state.hash, state.device_id,
                 state.appliance_api_parsing, state.filter_config_topics);
      }
    }
  }

  // ── Wait then reboot ─────────────────────────────────────────────────────
  if (this->ota_reboot_pending_) {
    esp_task_wdt_reset();
    uint32_t elapsed = esphome::millis() - this->ota_reboot_start_ms_;
    if (elapsed >= 5000) {
      ESP_LOGI(TAG, "Rebooting after OTA cleanup + republish...");
      this->ota_reboot_pending_ = false;
      this->reboot_callback_();
    }
  }
#endif
}

}  // namespace geappliances_bridge
}  // namespace esphome