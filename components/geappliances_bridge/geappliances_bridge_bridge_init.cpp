/*!
 * @file
 * @brief Bridge startup initializers and ongoing bridge lifecycle management.
 *
 * MODULE GOAL: Own every one-time initialization step for the MQTT client
 * adapter and the bridge HSMs, plus feature-bit reading startup and the
 * AUTO-mode subscription-activity watchdog.
 *
 * start_feature_bit_reading_() is called by the startup HSM to kick off
 * the feature-bit ERD reads once the MQTT client adapter is ready.
 *
 * initialize_mqtt_client_() runs once as soon as the device ID is ready
 * (Phase 4), before feature bit reading.  It binds the MQTT client adapter
 * to the device ID, sets up the ERD registry (registered-ERD tracking and
 * string-ERD type detection), and passes the registry to the adapter so it
 * is ready to publish ERD values immediately.
 *
 * initialize_erd_bridge_() runs once after feature bit reading and MQTT are
 * both ready (Phase 6).  It applies the valid-ERD filter to the registry
 * (built from feature bit results), selects the operating mode
 * (poll / subscribe / auto), and initializes the appropriate bridge HSMs.
 *
 * handle_subscription_failed() / handle_polling_failed() are called from the
 * startup HSM to handle bridge failures and trigger fallback to polling.
 */

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

namespace esphome {
namespace geappliances_bridge {
// ---------------------------------------------------------------------------
// Polling bridge discovery-complete callback (shared by all three init paths)
// ---------------------------------------------------------------------------

void GeappliancesBridge::on_poll_discovery_complete_()
{
  tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_bridge_ready, nullptr);
}


// ---------------------------------------------------------------------------
// Build the poll list using the erd_poll_list_builder module
// ---------------------------------------------------------------------------

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
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->polling_interval_ms_,
      this->autodiscovery_manager_.get_host_address(),
      this->poll_probe_list_,
      this->poll_probe_list_count_,
      &this->erd_cache_);
  this->polling_bridge_initialized_ = true;
}

// ---------------------------------------------------------------------------
// Startup: kick off feature-bit reading sequence
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_feature_bit_reading_()
{
  // Guard: don't re-initialize if the manager has already started.
  // Any READING_* state means the manager is actively processing,
  // PARSING/COMPLETE mean it's past the reading phase.
  // Note: the feature_bit_reading_started_ flag prevents re-init while the first read is in-flight.
  FeatureBitState state = this->feature_bit_manager_.get_state();
  if (state != FEATURE_BIT_STATE_READING) {
    return;
  }
  // Additional guard: if start() was already called and the first read
  // is in-flight, the manager is still in READING_0092 but read_queued_
  // is true. We can't check read_queued_ directly (it's private), but
  // we track whether we've already kicked off feature bit reading via
  // the feature_bit_reading_started_ flag.
  if (this->feature_bit_reading_started_) {
    return;
  }

  // Guard: only proceed once a valid ERD client is available. If the client
  // is null (e.g. autodiscovery not yet complete), leave the flag unset so
  // the next loop() call retries rather than getting permanently stuck.
  i_tiny_gea3_erd_client_t* erd_client = this->autodiscovery_manager_.get_active_erd_client();
  if (erd_client == nullptr) {
    return;
  }

  // Set the flag only after confirming init() will succeed, so that a
  // transient null-client on an earlier call does not permanently block retry.
  this->feature_bit_reading_started_ = true;

  this->feature_bit_manager_.init(
      erd_client,
      this->autodiscovery_manager_.get_host_address(),
      &this->timer_group_);
  this->feature_bit_manager_.start();
}

// ---------------------------------------------------------------------------
// Phase 4: Initialize the MQTT client adapter (called once from loop())
// ---------------------------------------------------------------------------

void GeappliancesBridge::initialize_mqtt_client_()
{
  if (this->mqtt_client_adapter_initialized_) {
    return;
  }

  // For manual device_id configs where autodiscovery is skipped (gea2_uart only,
  // no GEA3 uart), mark the protocol as GEA2 so run_protocol_stack_() enables
  // the GEA2 tight loop even before autodiscovery runs.
  if (this->autodiscovery_manager_.get_active_erd_client() == nullptr) {
    if (this->uart_ == nullptr) {
      this->gea2_protocol_active_ = true;
    }
  }

  // Bind the adapter to the device ID.
  esphome_mqtt_client_adapter_init(&this->mqtt_client_adapter_,
                                   this->device_identity_manager_.get_device_id());

  // Wire up the ERD registry: clears any stale registrations and sets up
  // the MQTT adapter with a single pointer for valid-ERD filtering and
  // registered-ERD tracking.
  this->erd_registry_.clear_registered_erds();
  esphome_mqtt_client_adapter_set_erd_registry(
    &this->mqtt_client_adapter_, &this->erd_registry_);

  this->mqtt_client_adapter_initialized_ = true;
}

// ---------------------------------------------------------------------------
// Phase 6: Initialize the ERD bridge (called once from loop())
// ---------------------------------------------------------------------------

void GeappliancesBridge::initialize_erd_bridge_()
{
  if (!this->mqtt_client_adapter_initialized_ || this->erd_bridge_initialized_) {
    return;
  }

  // Skip if no ERD client is configured (no UART).
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

  // Select operating mode.
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
    // Mark bridge initialized BEFORE the probe phase starts, so that if
    // the probe completes synchronously and fires signal_bridge_ready,
    // check_steady_state() in the running entry can see the flag.
    this->erd_bridge_initialized_ = true;
  }

  // Initialize the subscription bridge for non-polling modes (subscribe, auto).
  // In polling mode (GEA2 or explicit poll), subscriptions are not used, but
  // the bridge is still initialized above for custom ERD subscription support.
  if (!use_polling) {

    erd_bridge_subscribe_init(
      &this->erd_bridge_subscribe_,
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->autodiscovery_manager_.get_host_address(),
      &this->erd_cache_);
    this->subscription_bridge_initialized_ = true;
    this->erd_bridge_initialized_ = true;

    // Subscription bridge has no discovery phase — signal the startup HSM
    // immediately so it can transition to subscription_watch.
    tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_bridge_ready, nullptr);
  }
  // Initialize the write bridge. Autodiscovery is complete by this point,
  // so we have the real host address from the broadcast FF 0x0008 response.
  uint8_t host_addr = this->autodiscovery_manager_.get_host_address();
  erd_write_bridge_init(
    &this->erd_write_bridge_,
    &this->timer_group_,
    this->autodiscovery_manager_.get_active_erd_client(),
    &this->mqtt_client_adapter_.interface,
    host_addr);
  this->write_bridge_initialized_ = true;

  // Subscribe to the wildcard write topic so incoming write commands from
  // Home Assistant are routed to the write bridge via on_write_request_event.
  esphome_mqtt_client_adapter_subscribe_write_topic(&this->mqtt_client_adapter_);
}

// ---------------------------------------------------------------------------
// Start custom ERD polling bridge (deferred: called after subscription settles)
// ---------------------------------------------------------------------------
// When in subscription mode with custom ERDs, this starts a polling bridge
// that polls only the custom ERDs alongside the subscription bridge.
// The subscription bridge continues to handle all standard ERDs, while the
// polling bridge handles custom ERDs that may not be covered by subscription.
// The polling list is allocated to the exact size needed.
// ---------------------------------------------------------------------------

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
    this->custom_erd_polling_started_ = true;
    return;
  }
  ESP_LOGI(TAG, "Custom polling: %u subscribed ERDs skipped, %u ERDs remaining",
           this->custom_erds_count_ - this->subscription_unseen_custom_erds_count_,
           this->subscription_unseen_custom_erds_count_);

  this->init_polling_bridge_(true, this->subscription_unseen_custom_erds_,
                             this->subscription_unseen_custom_erds_count_);
  this->custom_erd_polling_started_ = true;
}

void GeappliancesBridge::maybe_start_custom_erd_polling_()
{
  if (this->custom_erds_count_ == 0 ||
      this->custom_erd_polling_started_) {
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

// ---------------------------------------------------------------------------
// Check if the polling bridge (running alongside subscription) has failed,
// and if so, transition to full polling mode.
// ---------------------------------------------------------------------------

void GeappliancesBridge::handle_polling_failed()
{
  polling_state_t poll_state = this->get_polling_state();
  if (poll_state != polling_state_failed) {
    return;
  }

  if (this->subscription_bridge_initialized_) {
    // Custom ERD polling bridge failed alongside subscription — destroy it
    // and let subscription continue handling standard ERDs.
    ESP_LOGW(TAG, "Custom ERD polling bridge failed; continuing with subscription only");
    erd_bridge_poll_destroy(&this->erd_bridge_poll_);
    this->polling_bridge_initialized_ = false;
    this->custom_erd_polling_started_ = false;
  } else {
    // Primary polling bridge failed (POLL mode or GEA2). No fallback available.
    ESP_LOGE(TAG, "Primary polling bridge failed; no data path available");
    // Leave the bridge in failed state. The appliance_lost handler in
    // state_failed will re-probe if the appliance comes back.
  }
  this->last_logged_poll_state_ = polling_state_none;
}

// ---------------------------------------------------------------------------
// Subscription failed: fallback to polling
// ---------------------------------------------------------------------------

void GeappliancesBridge::handle_subscription_failed()
{
  // Already in polling mode — nothing to do.
  if (this->mode_ != BRIDGE_MODE_AUTO) {
    return;
  }

  // Tear down the subscription bridge.
  erd_bridge_subscribe_destroy(&this->erd_bridge_subscribe_);
  this->subscription_bridge_initialized_ = false;
  this->last_logged_poll_state_ = polling_state_none;
  this->last_logged_subscribe_state_ = subscription_state_none;

  // Destroy any existing polling bridge (e.g., from custom ERD polling)
  // before re-initializing to avoid leaking heap allocations.
  if (this->custom_erd_polling_started_) {
    erd_bridge_poll_destroy(&this->erd_bridge_poll_);
    this->custom_erd_polling_started_ = false;
    this->polling_bridge_initialized_ = false;
  }

  this->init_polling_bridge_(true);

  // Signal the startup HSM that subscription fallback has occurred.
  tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_subscription_fallback, nullptr);

  ESP_LOGI(TAG, "Successfully switched to polling mode");
}

}  // namespace geappliances_bridge
}  // namespace esphome
