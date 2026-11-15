#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "erd_cache.h"
#include <inttypes.h>
#include "ha_discovery_data.h"

#ifdef USE_ESP32
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esphome/core/preferences.h"
#elif defined(USE_ESP_IDF_STUBS)
#include "esp-idf/esp_task_wdt.h"
#endif

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

GEA_TAG(TAG) = "geappliances_bridge";

namespace esphome {
namespace geappliances_bridge {
static const char* startup_state_name(tiny_hsm_state_t state)
{
  if (state == startup_state_startup_delay)      return "Startup Delay";
  if (state == startup_state_autodiscovery)      return "Autodiscovery";
  if (state == startup_state_device_id)          return "Device ID";
  if (state == startup_state_mqtt_client_init)   return "MQTT Client Init";
  if (state == startup_state_feature_bits)       return "Feature Bits";
  if (state == startup_state_bridge_init)        return "Bridge Init";
  if (state == startup_state_subscription_watch) return "Subscription Watch";
  if (state == startup_state_running)            return "Running";
  return "Unknown";
}

static const char* bridge_mode_name(esphome::geappliances_bridge::BridgeMode mode, subscription_state_t sub_state)
{
  if (mode == esphome::geappliances_bridge::BRIDGE_MODE_POLL) return "Polling";
  if (mode == esphome::geappliances_bridge::BRIDGE_MODE_SUBSCRIBE) return "Subscription";
  if (mode == esphome::geappliances_bridge::BRIDGE_MODE_AUTO) {
    return subscription_is_active(sub_state) ? "Auto (Subscription)" : "Auto (Polling - fallback)";
  }
  return "Unknown";
}

void GeappliancesBridge::add_custom_erd(tiny_erd_t erd)
{
  if (this->custom_erds_count_ >= CUSTOM_ERDS_MAX) {
    ESP_LOGW(TAG, "Custom ERD 0x%04x dropped: capacity limit (%u) reached",
             (unsigned)erd, (unsigned)CUSTOM_ERDS_MAX);
    return;
  }
  this->custom_erds_[this->custom_erds_count_++] = {erd, PROBE_ENTRY_DEFAULT_ADDRESS};
}

void GeappliancesBridge::add_custom_erds(const uint16_t* erds, uint16_t count)
{
  if (erds == nullptr) return;
  for (uint16_t i = 0; i < count; i++) {
    this->add_custom_erd(erds[i]);
  }
}

void GeappliancesBridge::add_custom_erds_with_addresses(const uint16_t* erds, const uint8_t* addresses, uint16_t count)
{
  if (erds == nullptr) return;
  for (uint16_t i = 0; i < count; i++) {
    if (this->custom_erds_count_ >= CUSTOM_ERDS_MAX) {
      ESP_LOGW(TAG, "Custom ERD 0x%04x dropped: capacity limit (%u) reached",
               (unsigned)erds[i], (unsigned)CUSTOM_ERDS_MAX);
      return;
    }
    uint8_t addr = PROBE_ENTRY_DEFAULT_ADDRESS;
    if (addresses != nullptr) {
      addr = addresses[i];
    }
    this->custom_erds_[this->custom_erds_count_++] = {erds[i], addr};
  }
}



void GeappliancesBridge::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

  // Initialize the wire-level protocol stack: UART adapters, GEA2/GEA3
  // interfaces, both ERD clients, the GEA2→GEA3 adapter, and the shared
  // timer group every data-path component registers timers on.
  this->protocol_stack_.init(this->client_address_, this->uart_, this->gea2_uart_);

  erd_cache_init(&this->erd_cache_);
  erd_set_init(&this->custom_erd_subscription_seen_erds_);

  // Route ERD client activity to the bridge (see handle_erd_client_activity_).
  // The GEA2 adapter client re-publishes GEA2 activity as GEA3-typed events,
  // so one handler serves both protocols.
  if (this->protocol_stack_.has_gea3_uart()) {
    tiny_event_subscription_init(
      &this->erd_client_activity_subscription_,
      this,
      +[](void* context, const void* args) {
        auto self = reinterpret_cast<GeappliancesBridge*>(context);
        auto activity_args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
        self->handle_erd_client_activity_(activity_args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(this->protocol_stack_.get_gea3_client()),
      &this->erd_client_activity_subscription_);
  }
  if (this->protocol_stack_.has_gea2_uart()) {
    tiny_event_subscription_init(
      &this->gea2_activity_subscription_,
      this,
      +[](void* context, const void* args) {
        auto self = reinterpret_cast<GeappliancesBridge*>(context);
        auto activity_args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
        self->handle_erd_client_activity_(activity_args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(this->protocol_stack_.get_gea2_adapter_client()),
      &this->gea2_activity_subscription_);
  }

  // Initialize autodiscovery manager (timer-driven, self-driving)
  // Must be called AFTER ERD clients and UART adapters are initialized.
  this->autodiscovery_manager_.init(
      this->protocol_stack_.timer_group(),
      this->protocol_stack_.has_gea3_uart() ? this->protocol_stack_.get_gea3_client() : nullptr,
      this->protocol_stack_.has_gea2_uart() ? this->protocol_stack_.get_gea2_client() : nullptr,
      this->protocol_stack_.has_gea2_uart() ? this->protocol_stack_.get_gea2_adapter_client() : nullptr,
      this->protocol_stack_.has_gea3_uart() ? this->protocol_stack_.get_gea3_uart_adapter() : nullptr,
      this->protocol_stack_.has_gea2_uart() ? this->protocol_stack_.get_gea2_uart_adapter() : nullptr,
      this->protocol_stack_.has_gea3_uart(),
      this->protocol_stack_.has_gea2_uart(),
      this->client_address_);

  if (this->configured_device_id_[0] != '\0') {
    ESP_LOGI(TAG, "Device ID configured: %s (will be applied after autodiscovery)", this->configured_device_id_);
  } else {
    ESP_LOGI(TAG, "No device_id configured, will auto-generate after autodiscovery");
  }
  // NOTE: Discovery change detection (hash + device ID) is now done in
  // check_steady_state() where the device identity is available.

  ESP_LOGI(TAG, "Waiting %lu seconds before starting autodiscovery...",
           (unsigned long)(AUTODISCOVERY_STARTUP_DELAY_MS / 1000));

  this->discovery_lifecycle_manager_.init(
      &this->ha_discovery_manager_,
      this->device_identity_manager_,
      this->mqtt_client_adapter_,
      &this->erd_cache_,
      this->generate_device_config_,
      this->appliance_api_parsing_,
      this->filter_config_topics_,
      &GeappliancesBridge::discovery_is_ready_,
      this);
  this->diagnostic_sensor_publisher_.init(
      this->erd_publish_rate_sensor_,
      this->mqtt_publish_rate_sensor_,
      this->erd_cache_entries_sensor_,
      this->erd_cache_updates_sensor_,
      this->mqtt_disconnect_count_sensor_,
      this->mqtt_disconnect_duration_sensor_,
      this->erd_cache_,
      this->erd_cache_publisher_);

  ESP_LOGCONFIG(TAG, "GE Appliances Bridge setup complete");
}

void GeappliancesBridge::loop() {

  // Drive the GEA2/GEA3 protocol stack FIRST so that UART bytes are
  // processed before any MQTT work.  The tight loop must run before
  // MQTT operations to avoid starving UART processing on single-core
  // ESP32 variants where a blocking MQTT call can delay response
  // processing past the appliance's timeout window.
  //
  // GEA2 work is in flight while GEA2 autodiscovery is progressing; the
  // stack ORs in its own GEA2-active latch (manual device_id configs with
  // only a GEA2 UART) and gates on the GEA2 UART being configured.
  bool gea2_pending = this->autodiscovery_manager_.is_gea2_protocol() ||
    this->autodiscovery_manager_.get_state() == AUTODISCOVERY_GEA2_BROADCAST_PENDING ||
    this->autodiscovery_manager_.get_state() == AUTODISCOVERY_GEA2_BROADCAST_WAITING;
  uint32_t protocol_stack_start = esphome::millis();
  this->protocol_stack_.run(gea2_pending);
  uint32_t stack_elapsed = esphome::millis() - protocol_stack_start;
  if (stack_elapsed >= 1000) {
    ESP_LOGW(TAG, "Long protocol stack run: %lums (mode=%s, polling=%s)",
             (unsigned long)stack_elapsed,
             this->mode_ == BRIDGE_MODE_SUBSCRIBE ? "sub" : (this->mode_ == BRIDGE_MODE_AUTO ? "auto" : "poll"),
             this->data_plane_phase_ != DataPlanePhase::kIdle ? "yes" : "no");
  }

  /* Tick ERD publish cooldowns once per second.
   * Uses a member variable (not static) so it resets on re-init and
   * avoids the first-tick race on cold start. Unsigned subtraction
   * handles millis() wrap correctly. */
  if (this->throttle_rate_seconds_ > 0) {
    uint32_t now = esphome::millis();
    if (now - this->last_cooldown_tick_ >= 1000) {
      this->last_cooldown_tick_ = now;
      erd_cache_tick_cooldowns(&this->erd_cache_);
    }
  }
#ifdef USE_ESP32
  // Feed the task watchdog after the protocol stack — the GEA2 tight loop
  // can run for 200 ms wall-clock time, exceeding the default TWDT timeout.
  esp_task_wdt_reset();
#endif

  if (this->startup_hsm_wrapper_.hsm.current == nullptr) {
    startup_hsm_wrapper_init(&this->startup_hsm_wrapper_, this, startup_state_startup_delay);
  }

  uint32_t hsm_start = esphome::millis();
  tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_run_loop, nullptr);
  uint32_t hsm_elapsed = esphome::millis() - hsm_start;
  if (hsm_elapsed >= 1000) {
    ESP_LOGW(TAG, "Long HSM run_loop: %lums", (unsigned long)hsm_elapsed);
  }
#ifdef USE_ESP32
  // Feed the task watchdog after the HSM run_loop signal — in steady-state
  // this drains pending MQTT updates (each acquiring the IDF MQTT mutex)
  // and can block for hundreds of milliseconds.
  esp_task_wdt_reset();
#endif

  this->update_publisher_state_();

  this->discovery_lifecycle_manager_.loop();

  this->diagnostic_sensor_publisher_.loop();
}

  /* ERD cache publisher state machine:
   *   kIdle     — publisher active; HA discovery not processing.
   *   kPaused   — publisher paused while ha_discovery_manager_is_processing()
   *               is true.
   *   kResuming — resumed after a pause; cleared once first_round_done()
   *               confirms the post-resume drain finished. */
void GeappliancesBridge::update_publisher_state_()
{
  bool ha_discovery_active = ha_discovery_manager_is_processing(&this->ha_discovery_manager_);

  if (ha_discovery_active) {
    if (this->erd_cache_publisher_.cache != nullptr) {
      erd_cache_mqtt_publisher_pause(&this->erd_cache_publisher_);
      if (this->publisher_pause_phase_ != PublisherPausePhase::kPaused) {
        ESP_LOGD(TAG, "ERD cache publisher paused during MQTT discovery payload generation");
        this->publisher_pause_phase_ = PublisherPausePhase::kPaused;
      }
    }
  } else {
    if (this->erd_cache_publisher_.cache != nullptr) {
      erd_cache_mqtt_publisher_resume(&this->erd_cache_publisher_);
      if (this->publisher_pause_phase_ == PublisherPausePhase::kPaused) {
        ESP_LOGD(TAG, "ERD cache publisher resumed after MQTT discovery payload generation");
        this->publisher_pause_phase_ = PublisherPausePhase::kResuming;
      }
    }
  }

  /* Check steady state BEFORE this function's own signal_work() below - the
   * background task sets first_round_done during its drain, and this read wants
   * the value from before that give can wake it again.  Pacing may defer a wake;
   * it can never advance one past this read.  The read is NOT ordered against
   * every wake: resume() above signals work on this same iteration whenever the
   * cache is attached, whether or not discovery had paused the publisher. */
  if (this->publisher_pause_phase_ == PublisherPausePhase::kResuming &&
      erd_cache_mqtt_publisher_first_round_done(&this->erd_cache_publisher_)) {
    ESP_LOGI(TAG, "Device is in steady state");
    this->publisher_pause_phase_ = PublisherPausePhase::kIdle;
  }

  if (this->erd_cache_publisher_.cache != nullptr && !ha_discovery_active) {
    erd_cache_mqtt_publisher_signal_work(&this->erd_cache_publisher_);
  }
}


void GeappliancesBridge::log_poll_state_transitions_()
{
  if (this->data_plane_phase_ == DataPlanePhase::kIdle) {
    return;
  }

  subscription_state_t sub_state = this->get_subscription_state();

  if (phase_has_polling_bridge(this->data_plane_phase_)) {
    polling_state_t poll_state = this->get_polling_state();
    if (poll_state != polling_state_none && poll_state != this->last_logged_poll_state_) {
      this->last_logged_poll_state_ = poll_state;
      ESP_LOGD(TAG, "Polling bridge state: %s", polling_state_name(poll_state));
    }
  }

  if (phase_has_subscription_bridge(this->data_plane_phase_)) {
    if (sub_state != subscription_state_none && sub_state != this->last_logged_subscribe_state_) {
      ESP_LOGI(TAG, "Subscription bridge state: %s (ERDs cached: %u)",
               subscription_state_name(sub_state), erd_cache_get_count(&this->erd_cache_));
      this->last_logged_subscribe_state_ = sub_state;
    }
  }
}

void GeappliancesBridge::handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args) {
  if (this->data_plane_phase_ != DataPlanePhase::kIdle &&
      args->address == this->autodiscovery_manager_.get_host_address() &&
      args->type == tiny_gea3_erd_client_activity_type_subscription_publication_received) {
    erd_set_insert(&this->custom_erd_subscription_seen_erds_, args->subscription_publication_received.erd);
  }

  // Device ID reads (after discovery, before bridge init)
  // Note: FeatureBitManager subscribes directly to ERD client activity events,
  // so the bridge no longer routes feature bit ERDs to it.
  if (this->data_plane_phase_ == DataPlanePhase::kIdle && args->address == this->autodiscovery_manager_.get_host_address()) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed) {
      tiny_erd_t erd = args->read_completed.erd;
      const uint8_t* data = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t size = args->read_completed.data_size;
      if (!this->should_route_to_feature_bits_(erd)) {
        this->device_identity_manager_.on_erd_read_completed(erd, data, size);
      }
    } else if (args->type == tiny_gea3_erd_client_activity_type_read_failed) {
      tiny_erd_t erd = args->read_failed.erd;
      if (!this->should_route_to_feature_bits_(erd)) {
        this->device_identity_manager_.on_erd_read_failed(erd);
      }
    }
  }
}

bool GeappliancesBridge::should_route_to_feature_bits_(tiny_erd_t erd)
{
  FeatureBitState state = this->feature_bit_manager_.get_state();
  bool feature_bit_active = (state != FEATURE_BIT_STATE_COMPLETE &&
                            state != FEATURE_BIT_STATE_FAILED);
  return feature_bit_active && is_feature_bit_erd(erd);
}

void GeappliancesBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "GE Appliances Bridge:");
  if (this->configured_device_id_[0] != '\0') {
    ESP_LOGCONFIG(TAG, "  Configured Device ID: %s", this->configured_device_id_);
  }
  {
    const char* device_id = this->device_identity_manager_.get_device_id();
    if (device_id != nullptr && device_id[0] != '\0') {
      ESP_LOGCONFIG(TAG, "  Device ID: %s", device_id);
    }
  }
  ESP_LOGCONFIG(TAG, "  Client Address: 0x%02X", this->client_address_);
  if (this->board_address_configured_) {
    ESP_LOGCONFIG(TAG, "  Board Address: 0x%02X (configured)", (unsigned)this->board_address_);
  }
  ESP_LOGCONFIG(TAG, "  Host Address: 0x%02X", this->autodiscovery_manager_.get_host_address());
  if (this->uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA3 UART: configured (baud %lu)", baud);
  }
  if (this->gea2_uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA2 UART: configured (baud %u)", 19200u);
  }
  if (this->autodiscovery_manager_.get_state() == AUTODISCOVERY_COMPLETE) {
    ESP_LOGCONFIG(TAG, "  Active Protocol: %s", this->autodiscovery_manager_.is_gea2_protocol() ? "GEA2" : "GEA3");
  }
  // Display bridge mode — compute into locals so the helper functions are
  // actually called even when ESP_LOGCONFIG is stubbed to ((void)0) in tests.
  const char* mode_str = bridge_mode_name(this->mode_, this->get_subscription_state());
  const char* phase_str = startup_state_name(this->startup_hsm_wrapper_.hsm.current);
  (void)mode_str;
  (void)phase_str;

  ESP_LOGCONFIG(TAG, "  Mode: %s", mode_str);

  {
    subscription_state_t sub_state = this->get_subscription_state();
    if (this->mode_ == BRIDGE_MODE_POLL || !subscription_is_active(sub_state)) {
      ESP_LOGCONFIG(TAG, "  Polling Interval: %lu ms", (unsigned long)this->polling_interval_ms_);
    }
  }
  ESP_LOGCONFIG(TAG, "  Appliance API Parsing: %s", this->appliance_api_parsing_ ? "enabled" : "disabled");
  if (this->feature_bit_manager_.get_state() == FEATURE_BIT_STATE_COMPLETE) {
    ESP_LOGCONFIG(TAG, "  Appliance API Valid ERDs: %u", this->feature_bit_manager_.get_valid_erd_count());
  }
  if (phase_has_polling_bridge(this->data_plane_phase_)) {
    polling_state_t poll_state = this->get_polling_state();
    const char* poll_state_str = polling_state_name(poll_state);
    if (poll_state_str != nullptr) {
      ESP_LOGCONFIG(TAG, "  Polling Bridge State: %s", poll_state_str);
    }
  }
  if (this->custom_erds_count_ > 0) {
    // One summary line, not one per ERD: this loop was the largest single
    // block of the boot log (65-92 lines on real appliances), and the board
    // distribution is the only part it says that matters. Per-ERD detail is
    // kept at VERBOSE. The tally is computed into locals so the helpers run
    // even when the log macros are stubbed out in tests.
    board_tally_t tally;
    board_tally_init(&tally);
    for (uint16_t i = 0; i < this->custom_erds_count_; i++) {
      board_tally_add(&tally, this->custom_erds_[i].board_address);
    }
    char boards[BOARD_TALLY_BUF_SIZE];
    board_tally_format(&tally, boards, sizeof(boards));
    ESP_LOGD(TAG, "  Custom ERDs: %u configured (%s)", this->custom_erds_count_, boards);
    for (uint16_t i = 0; i < this->custom_erds_count_; i++) {
      const custom_erd_entry_t &entry = this->custom_erds_[i];
      if (entry.board_address == PROBE_ENTRY_DEFAULT_ADDRESS) {
        ESP_LOGV(TAG, "    ERD 0x%04X: primary board", entry.erd);
      } else {
        ESP_LOGV(TAG, "    ERD 0x%04X: address 0x%02X", entry.erd, entry.board_address);
      }
    }
  }

  ESP_LOGCONFIG(TAG, "  Startup State: %s", phase_str);
}

  // Run after MQTT (priority 50), same priority as UART (priority 600)
float GeappliancesBridge::get_setup_priority() const {
  return setup_priority::DATA;
}

bool GeappliancesBridge::teardown() {
  // Reset protocol-stack per-run state (GEA2 tick tracking, the GEA2-active
  // latch) so re-init starts fresh.
  this->protocol_stack_.teardown();
  // Clear the seen ERDs set for symmetry with erd_set_init() in setup().
  erd_set_clear(&this->custom_erd_subscription_seen_erds_);
  // Reset the feature bit failure log flag for correct behavior after re-init.
  this->feature_bit_failure_logged_ = false;
  startup_hsm_wrapper_destroy(&this->startup_hsm_wrapper_);
  this->feature_bit_manager_.cleanup();
  this->autodiscovery_manager_.cleanup();
  this->device_identity_manager_.cleanup();
  // Destroy whichever bridge(s) the data-plane phase says exist. The phase
  // never leaves kIdle before teardown, so this is unambiguous and prevents
  // double-free or missed cleanup.
  if (phase_has_subscription_bridge(this->data_plane_phase_)) {
    erd_bridge_subscribe_destroy(&this->erd_bridge_subscribe_);
  }
  if (phase_has_polling_bridge(this->data_plane_phase_)) {
    erd_bridge_poll_destroy(&this->erd_bridge_poll_);
  }
  if (this->data_plane_phase_ != DataPlanePhase::kIdle) {
    erd_write_bridge_destroy(&this->erd_write_bridge_);
  }
  this->data_plane_phase_ = DataPlanePhase::kIdle;

  // Clean up the HA discovery manager before the ERD cache publisher.
  // The discovery manager holds pointers to erd_cache_ and mqtt_client_adapter_
  // which are destroyed later in the teardown sequence.
  ha_discovery_manager_cleanup(&this->ha_discovery_manager_);

  // Destroy the ERD cache publisher before the adapter is destroyed.
  if (this->erd_cache_publisher_.cache) {
    erd_cache_mqtt_publisher_destroy(&this->erd_cache_publisher_);
  }
  erd_cache_destroy(&this->erd_cache_);

  // Free heap-allocated members of the MQTT client adapter to prevent
  // memory leaks (device_id string, pending_updates map, etc.).
  if (this->mqtt_client_adapter_.initialized) {
    esphome_mqtt_client_adapter_destroy(&this->mqtt_client_adapter_);
  }
  Component::teardown();
  return true;
}

GeappliancesBridge::~GeappliancesBridge() {
  this->teardown();
}

// =============================================================================
// IBridgeServices implementation
//
// These thin wrappers are the only entry points the startup HSM is allowed to
// use when interacting with the bridge. Each delegates to an existing internal
// method or member, keeping the HSM decoupled from GeappliancesBridge internals.
// =============================================================================

void GeappliancesBridge::run_autodiscovery()
{
  bool has_gea3_client = this->protocol_stack_.has_gea3_uart() &&
    this->protocol_stack_.get_gea3_client()->api != nullptr;
  bool has_gea2_client = this->protocol_stack_.has_gea2_uart() &&
    this->protocol_stack_.get_gea2_adapter_client()->api != nullptr;
  if (!has_gea3_client && !has_gea2_client) {
    return;
  }

  if (this->board_address_configured_) {
    ESP_LOGI(TAG, "Using configured board address 0x%02X for targeted discovery",
             (unsigned)this->board_address_);
    this->autodiscovery_manager_.set_target_address(this->board_address_);
  }

  this->autodiscovery_manager_.start();
}

bool GeappliancesBridge::is_autodiscovery_complete() const
{
  return autodiscovery_manager_.get_state() == AUTODISCOVERY_COMPLETE;
}

uint8_t GeappliancesBridge::get_discovered_host_address() const
{
  return autodiscovery_manager_.get_host_address();
}

bool GeappliancesBridge::is_discovered_gea2_protocol() const
{
  return autodiscovery_manager_.is_gea2_protocol();
}

void GeappliancesBridge::init_device_id_reading()
{
  if (device_identity_manager_.get_state() != DEVICE_ID_STATE_COMPLETE) {
    device_identity_manager_.init(
        configured_device_id_,
        autodiscovery_manager_.get_active_erd_client(),
        autodiscovery_manager_.get_host_address());
  }
}

bool GeappliancesBridge::is_device_id_complete() const
{
  return device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE;
}

bool GeappliancesBridge::is_mqtt_client_initialized() const
{
  return this->mqtt_client_adapter_.initialized;
}

void GeappliancesBridge::initialize_mqtt_client()
{
  initialize_mqtt_client_();
}

void GeappliancesBridge::start_feature_bit_reading()
{
  start_feature_bit_reading_();
}

bool GeappliancesBridge::is_feature_bits_complete() const
{
  FeatureBitState state = feature_bit_manager_.get_state();
  if (state == FEATURE_BIT_STATE_FAILED && !feature_bit_failure_logged_) {
    feature_bit_failure_logged_ = true;
    ESP_LOGW(TAG, "Feature bit parsing failed; falling back to full polling mode");
  }
  return state == FEATURE_BIT_STATE_COMPLETE || state == FEATURE_BIT_STATE_FAILED;
}
void GeappliancesBridge::record_startup_delay_start()
{
  startup_delay_start_ms_ = millis();
}

bool GeappliancesBridge::is_startup_delay_elapsed() const
{
  return millis() - startup_delay_start_ms_ >= AUTODISCOVERY_STARTUP_DELAY_MS;
}

bool GeappliancesBridge::is_bridge_initialized() const
{
  return this->data_plane_phase_ != DataPlanePhase::kIdle;
}

void GeappliancesBridge::initialize_erd_bridge()
{
  initialize_erd_bridge_();
}

BridgeMode GeappliancesBridge::get_mode() const
{
  return mode_;
}

subscription_state_t GeappliancesBridge::get_subscription_state() const
{
  if (!phase_has_subscription_bridge(this->data_plane_phase_)) {
    return subscription_state_none;
  }
  return this->erd_bridge_subscribe_.current_state;
}

polling_state_t GeappliancesBridge::get_polling_state() const
{
  if (!phase_has_polling_bridge(this->data_plane_phase_)) {
    return polling_state_none;
  }
  return this->erd_bridge_poll_.current_state;
}
bool GeappliancesBridge::check_steady_state()
{
  bool steady = this->data_plane_readiness_ == DataPlaneReadiness::kNotSteady &&
    (!phase_has_subscription_bridge(this->data_plane_phase_) ||
     this->erd_bridge_subscribe_.current_state == subscription_state_steady) &&
    (!phase_has_polling_bridge(this->data_plane_phase_) ||
     this->erd_bridge_poll_.current_state == polling_state_polling) &&
    this->data_plane_phase_ != DataPlanePhase::kIdle;

  if (steady) {
    this->data_plane_readiness_ = DataPlaneReadiness::kSteady;
    ESP_LOGI(TAG, "Appliance Bridge is in steady state (ERDs cached: %u, arena: %u/%u bytes, %u%%)",
             erd_cache_get_count(&this->erd_cache_),
             erd_cache_get_arena_usage(&this->erd_cache_),
             ERD_CACHE_ARENA_SIZE,
             erd_cache_get_arena_usage_percent(&this->erd_cache_));

    /* One line per section, at DEBUG. The per-entry detail that used to
     * accompany this (per-ERD config dump, per-cache-add, per-skipped feature
     * ERD) is at VERBOSE now: ~200 boot lines of it overflowed the 30-slot
     * MQTT log queue on the busier appliances, which is what the "Dropped N
     * messages" warning was reporting. */
    if (phase_has_subscription_bridge(this->data_plane_phase_)) {
      ESP_LOGD(TAG, "  Subscription: %u ERDs (host board 0x%02X)",
               this->erd_bridge_subscribe_.erd_set.count,
               this->erd_bridge_subscribe_.erd_host_address);
    }
    {
      polled_erds_state_t polled;
      polled.polling_bridge_initialized = phase_has_polling_bridge(this->data_plane_phase_);
      polled.custom_erds_count = this->custom_erds_count_;
      polled.unseen_custom_count = this->subscription_unseen_custom_erds_count_;

      /* The wording, the "the bridge already printed the count" suppression and the
       * reason clauses live in board_polled_summary_format (erd_bridge_common.h),
       * where the unit tests can see them.  The per-board split is read out of the
       * running bridge's own list rather than subscription_unseen_custom_erds_:
       * the probe phase drops unresponsive ERDs, so the built list is larger than
       * what is polled and its split would not sum to the count printed beside it.
       * The label stays "Polled ERDs" rather than "Poll list:" or "Polling bridge:"
       * because bridge_init logs the list as built and erd_bridge_poll logs the
       * list handed to the bridge, and those counts differ by design. */
      char polled_line[BOARD_POLLED_SUMMARY_BUF_SIZE];
      if (board_polled_summary_format(&polled, this->erd_bridge_poll_.erd_polling_addresses,
                                      this->erd_bridge_poll_.polling_list_count,
                                      polled_line, sizeof(polled_line)) > 0) {
        ESP_LOGD(TAG, "  %s", polled_line);
      }
    }
    if (this->feature_bit_manager_.get_state() == FEATURE_BIT_STATE_COMPLETE) {
      ESP_LOGD(TAG, "  Feature bits: %u valid, %u skipped (unsupported or unreadable)",
               this->feature_bit_manager_.get_valid_erd_count(),
               this->feature_bit_manager_.get_skipped_erd_count());
    }

    if (this->generate_device_config_) {
      this->discovery_lifecycle_manager_.check_discovery_changes(
          this->device_identity_manager_.get_device_id());
    }
  }

  return steady;
}
bool GeappliancesBridge::is_erd_cache_publisher_initialized() const
{
  return erd_cache_publisher_.cache != nullptr;
}

void GeappliancesBridge::init_erd_cache_publisher_()
{
  if (this->erd_cache_publisher_.cache) return; // already initialized

  /* Apply rate limit configuration before starting the publisher.
   * With the ESP-IDF framework the background task starts immediately in init() and
   * could drain cache entries before the rate limit takes effect. */
  erd_cache_set_throttle_rate_seconds(&this->erd_cache_, this->throttle_rate_seconds_);

  erd_cache_mqtt_publisher_init(
    &this->erd_cache_publisher_,
    &this->erd_cache_,
    &this->mqtt_client_adapter_.interface,
    this->device_identity_manager_.get_device_id(),
    this->autodiscovery_manager_.get_host_address());

  erd_cache_mqtt_publisher_start(&this->erd_cache_publisher_);

  ha_discovery_manager_init(&this->ha_discovery_manager_);
}

// Steady state and MQTT initialization are the bridge's own state; the
// discovery lifecycle asks instead of holding a reference to either.
bool GeappliancesBridge::discovery_is_ready_(void* context) {
  auto* self = static_cast<GeappliancesBridge*>(context);
  return self->data_plane_readiness_ == DataPlaneReadiness::kSteady && self->mqtt_client_adapter_.initialized;
}

void GeappliancesBridge::trigger_discovery_refresh()
{
  this->discovery_lifecycle_manager_.trigger_discovery_refresh();
}

void GeappliancesBridge::maybe_start_custom_erd_polling()
{
  maybe_start_custom_erd_polling_();
}

void GeappliancesBridge::log_poll_state_transitions()
{
  log_poll_state_transitions_();
}

void GeappliancesBridge::initialize_erd_cache_publisher()
{
  init_erd_cache_publisher_();
}

}  // namespace geappliances_bridge
}  // namespace esphome
