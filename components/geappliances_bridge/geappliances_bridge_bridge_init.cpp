
#include <cstring>
#include "geappliances_bridge.h"
#include "appliance_api_feature_lists.h"
#include "geappliances_bridge_constants.h"
#include "geappliances_bridge_startup_hsm.h"
#include "esphome/core/log.h"
#include "tiny_gea_constants.h"
#include "erd_poll_list_builder.h"
#include "erd_cache.h"
#include "erd_bridge_common.h"

GEA_TAG(TAG) = "geappliances_bridge_bridge_init";

// The write bridge copies MQTT write payloads into fixed slots sized by
// ERD_WRITE_MAX_DATA_SIZE; the MQTT adapter hands it buffers sized by
// ERD_CACHE_MAX_DATA_SIZE. Both encode the same GEA3 on-wire limit, so they
// must not drift apart — a smaller write slot would silently truncate a
// payload the adapter accepted.
static_assert(ERD_WRITE_MAX_DATA_SIZE == ERD_CACHE_MAX_DATA_SIZE,
              "write payload limit drifted from the ERD cache payload limit");

namespace esphome {
namespace geappliances_bridge {

void GeappliancesBridge::on_poll_discovery_complete_()
{
  tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_bridge_ready, nullptr);
}

ErdPollListResult build_poll_list_(GeappliancesBridge* bridge,
                                   const probe_entry_t* custom_erds,
                                   uint16_t custom_erds_count)
{
  ErdPollListConfig config;
  config.mode = bridge->mode_;
  {
    subscription_state_t sub_state = bridge->get_subscription_state();
    config.subscription_active = subscription_is_active(sub_state);
  }
  config.appliance_api_parsing = bridge->appliance_api_parsing_;
  config.feature_bit_valid_erds = bridge->feature_bit_manager_.get_valid_erds();
  config.feature_bit_valid_erds_count = bridge->feature_bit_manager_.get_valid_erd_count();
  config.custom_erds = custom_erds ? custom_erds :
      (bridge->custom_erds_count_ > 0 ? reinterpret_cast<const probe_entry_t*>(bridge->custom_erds_) : nullptr);
  config.custom_erds_count = custom_erds ? custom_erds_count : bridge->custom_erds_count_;
  config.appliance_type = bridge->device_identity_manager_.get_appliance_type();
  return build_erd_poll_list(config);
}

void GeappliancesBridge::init_polling_bridge_(bool log_as_info,
                                              const probe_entry_t* custom_erds,
                                              uint16_t custom_erds_count)
{
  this->erd_bridge_poll_.on_discovery_complete = +[](void* ctx) {
    reinterpret_cast<GeappliancesBridge*>(ctx)->on_poll_discovery_complete_();
  };
  this->erd_bridge_poll_.on_discovery_complete_context = this;

  auto result = build_poll_list_(this, custom_erds, custom_erds_count);
  this->poll_probe_list_count_ = result.erds_count;
  std::memcpy(this->poll_probe_list_, result.erds, result.erds_count * sizeof(probe_entry_t));

  if (log_as_info) {
    ESP_LOGI(TAG, "Poll list: %s (%u ERDs)", result.description, result.erds_count);
  } else {
    ESP_LOGD(TAG, "Poll list: %s (%u ERDs)", result.description, result.erds_count);
  }

  erd_bridge_poll_init(
      &this->erd_bridge_poll_,
      this->protocol_stack_.timer_group(),
      this->autodiscovery_manager_.get_active_erd_client(),
      this->polling_interval_ms_,
      this->autodiscovery_manager_.get_host_address(),
      this->poll_probe_list_,
      this->poll_probe_list_count_,
      &this->erd_cache_);
}

// ---------------------------------------------------------------------------
// Startup: kick off feature-bit reading sequence
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_feature_bit_reading_()
{
  // Guard: don't re-initialize while the manager already holds an ERD
  // client — init() subscribes activity and queues the first read, so a
  // second call while reads are in flight (or after PARSING/FAILED/COMPLETE)
  // would double-queue. cleanup() resets the client pointer, re-arming the
  // guard for a re-init.
  if (this->feature_bit_manager_.is_initialized()) {
    return;
  }

  // Guard: only proceed once a valid ERD client is available. If the client
  // is null (e.g. autodiscovery not yet complete), return so the next loop()
  // call retries rather than getting permanently stuck.
  i_tiny_gea3_erd_client_t* erd_client = this->autodiscovery_manager_.get_active_erd_client();
  if (erd_client == nullptr) {
    return;
  }

  this->feature_bit_manager_.init(
      erd_client,
      this->autodiscovery_manager_.get_host_address(),
      this->protocol_stack_.timer_group());
  this->feature_bit_manager_.start();
}

void GeappliancesBridge::initialize_mqtt_client_()
{
  if (this->mqtt_client_adapter_.initialized) {
    return;
  }

  // For manual device_id configs where autodiscovery is skipped (gea2_uart
  // only, no GEA3 uart), mark GEA2 as the active protocol so the protocol
  // stack runs the GEA2 tight loop even before autodiscovery would run.
  if (this->autodiscovery_manager_.get_active_erd_client() == nullptr) {
    if (this->uart_ == nullptr) {
      this->protocol_stack_.set_gea2_active(true);
    }
  }

  esphome_mqtt_client_adapter_init(&this->mqtt_client_adapter_,
                                   this->device_identity_manager_.get_device_id());

}

void GeappliancesBridge::initialize_erd_bridge_()
{
  if (!this->mqtt_client_adapter_.initialized ||
      this->data_plane_phase_ != DataPlanePhase::kIdle) {
    return;
  }

  if (this->uart_ == nullptr && this->gea2_uart_ == nullptr) {
    return;
  }

  ESP_LOGI(TAG, "Initializing ERD bridge");

  // Apply the valid-ERD filter when appliance API parsing is enabled and
  // produced results. An empty set is ignored by the registry so all ERDs
  // continue to be published in that case.
  if (this->appliance_api_parsing_ &&
      this->feature_bit_manager_.get_state() == FEATURE_BIT_STATE_COMPLETE &&
      this->feature_bit_manager_.get_valid_erd_count() > 0) {
    this->erd_registry_.set_valid_erds(this->feature_bit_manager_.get_valid_erds(),
                                       this->feature_bit_manager_.get_valid_erd_count());
    // Merge custom ERDs so they always pass the filter.
    if (this->custom_erds_count_ > 0) {
      std::vector<tiny_erd_t> erd_ids(this->custom_erds_count_);
      for (uint16_t i = 0; i < this->custom_erds_count_; i++) {
        erd_ids[i] = this->custom_erds_[i].erd;
      }
      this->erd_registry_.add_valid_erds(erd_ids.data(), this->custom_erds_count_);
    }
  }

  // Wire the ErdRegistry to the subscription bridge so it filters
  // subscription publications before they enter the cache. When
  // appliance_api_parsing is disabled, the registry has no filter active
  // (is_valid returns true for all ERDs), so this is a no-op.
  erd_bridge_subscribe_set_erd_registry(&this->erd_bridge_subscribe_, &this->erd_registry_);

  bool        use_polling = false;
  const char* mode_name = "unknown";

  if (this->autodiscovery_manager_.is_gea2_protocol()) {
    use_polling = true;
    mode_name   = "polling (GEA2 - subscriptions not supported)";
  } else if (this->mode_ == BRIDGE_MODE_POLL) {
    use_polling = true;
    mode_name   = "polling";
  } else if (this->mode_ == BRIDGE_MODE_SUBSCRIBE) {
    use_polling = false;
    mode_name   = "subscription";
  } else if (this->mode_ == BRIDGE_MODE_AUTO) {
    use_polling = false;
    mode_name   = "auto (subscription + custom ERD polling)";
  }

  ESP_LOGI(TAG, "Bridge mode: %s", mode_name);
  (void)mode_name; /* suppress -Wunused-but-set-variable when ESP_LOGI is stubbed */

  if (use_polling) {
    this->init_polling_bridge_(false);
    // Mark the data plane active BEFORE the probe phase starts, so that if
    // the probe completes synchronously and fires signal_bridge_ready,
    // check_steady_state() in the running entry can see the phase.
    this->data_plane_phase_ = DataPlanePhase::kPolling;
  }

  // Initialize the subscription bridge for non-polling modes (subscribe, auto).
  // In polling mode (GEA2 or explicit poll), subscriptions are not used, but
  // the bridge is still initialized above for custom ERD subscription support.
  if (!use_polling) {

    erd_bridge_subscribe_init(
      &this->erd_bridge_subscribe_,
      this->protocol_stack_.timer_group(),
      this->autodiscovery_manager_.get_active_erd_client(),
      this->autodiscovery_manager_.get_host_address(),
      &this->erd_cache_);
    this->data_plane_phase_ = DataPlanePhase::kSubscription;

    // Subscription bridge has no discovery phase — signal the startup HSM
    // immediately so it can transition to subscription_watch.
    tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_bridge_ready, nullptr);
  }
  // Initialize the write bridge. Autodiscovery is complete by this point,
  // so we have the real host address from the broadcast FF 0x0008 response.
  uint8_t host_addr = this->autodiscovery_manager_.get_host_address();
  // The write bridge subscribes to the wildcard write topic so incoming write
  // commands from Home Assistant are routed to it via on_write_request_event.
  erd_write_bridge_init(
    &this->erd_write_bridge_,
    this->protocol_stack_.timer_group(),
    this->autodiscovery_manager_.get_active_erd_client(),
    &this->mqtt_client_adapter_.interface,
    host_addr);
  esphome_mqtt_client_adapter_subscribe_write_topic(&this->mqtt_client_adapter_);

}

void GeappliancesBridge::start_custom_erd_polling_()
{
  if (this->custom_erds_count_ == 0) {
    return;
  }
  this->subscription_unseen_custom_erds_count_ = 0;
  for (uint16_t i = 0; i < this->custom_erds_count_; i++) {
    const auto &entry = this->custom_erds_[i];
    // Subscription publications originate only from the primary host. A
    // matching ERD ID observed there does not cover the same ERD on another
    // board, so every explicit non-primary address must be polled.
    if (entry.board_address != PROBE_ENTRY_DEFAULT_ADDRESS ||
        !erd_set_contains(&this->custom_erd_subscription_seen_erds_, entry.erd)) {
      this->subscription_unseen_custom_erds_[this->subscription_unseen_custom_erds_count_++] = {
          entry.erd, entry.board_address};
    }
  }
  if (this->subscription_unseen_custom_erds_count_ == 0) {
    ESP_LOGI(TAG, "All %u custom ERDs are covered by subscription; custom polling not started",
             this->custom_erds_count_);
    this->data_plane_phase_ = DataPlanePhase::kSubscriptionSettled;
    return;
  }
  ESP_LOGI(TAG, "Custom polling: %u subscribed ERDs skipped, %u ERDs remaining",
           this->custom_erds_count_ - this->subscription_unseen_custom_erds_count_,
           this->subscription_unseen_custom_erds_count_);

  this->init_polling_bridge_(true, this->subscription_unseen_custom_erds_,
                             this->subscription_unseen_custom_erds_count_);
  this->data_plane_phase_ = DataPlanePhase::kSubscriptionCustomPolling;
}

void GeappliancesBridge::maybe_start_custom_erd_polling_()
{
  if (this->custom_erds_count_ == 0 ||
      this->data_plane_phase_ != DataPlanePhase::kSubscription) {
    return;
  }

  subscription_state_t sub_state = this->get_subscription_state();
  // Wait for the subscription bridge to reach steady state before starting
  // custom ERD polling. This gives the subscription bridge time to publish
  // its ERDs, so we can avoid redundant polling of ERDs already covered
  // by subscription.
  if (sub_state != subscription_state_steady) {
    return;
  }

  this->start_custom_erd_polling_();
}

void GeappliancesBridge::handle_polling_failed()
{
  polling_state_t poll_state = this->get_polling_state();
  if (poll_state != polling_state_failed) {
    return;
  }

  if (phase_has_subscription_bridge(this->data_plane_phase_)) {
    ESP_LOGW(TAG, "Custom ERD polling bridge failed; continuing with subscription only");
    erd_bridge_poll_destroy(&this->erd_bridge_poll_);
    this->data_plane_phase_ = DataPlanePhase::kSubscription;
  } else {
    ESP_LOGE(TAG, "Primary polling bridge failed; no data path available");
    // Leave the bridge in failed state. The appliance_lost handler in
    // state_failed will re-probe if the appliance comes back.
  }
  this->last_logged_poll_state_ = polling_state_none;
}

void GeappliancesBridge::handle_subscription_failed()
{
  if (this->mode_ != BRIDGE_MODE_AUTO) {
    return;
  }

  erd_bridge_subscribe_destroy(&this->erd_bridge_subscribe_);
  this->last_logged_poll_state_ = polling_state_none;
  this->last_logged_subscribe_state_ = subscription_state_none;

  // Destroy any existing polling bridge (e.g., from custom ERD polling)
  // before re-initializing to avoid leaking heap allocations.
  if (phase_has_polling_bridge(this->data_plane_phase_)) {
    erd_bridge_poll_destroy(&this->erd_bridge_poll_);
  }

  this->init_polling_bridge_(true);
  this->data_plane_phase_ = DataPlanePhase::kPolling;
  // The startup HSM owns the subscription_watch → running transition (its
  // run_loop handler transitions right after calling this); this handler only
  // rebuilds the data path.
  ESP_LOGI(TAG, "Successfully switched to polling mode");
}

}  // namespace geappliances_bridge
}  // namespace esphome
