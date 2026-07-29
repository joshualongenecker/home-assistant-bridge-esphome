#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome_time_source.h"
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
// -----------------------------------------------------------------------
// Helper: map a startup HSM state function pointer to a human-readable name
// -----------------------------------------------------------------------
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

// -----------------------------------------------------------------------
// Helper: map bridge mode (+ subscription state for auto mode) to a name
// -----------------------------------------------------------------------
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
  this->custom_erds_[this->custom_erds_count_++] = erd;
}

void GeappliancesBridge::add_custom_erds(const uint16_t* erds, uint16_t count)
{
  if (erds == nullptr) return;
  for (uint16_t i = 0; i < count; i++) this->add_custom_erd(erds[i]);
}

static const tiny_gea3_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};

// GEA2 ERD client: one internal retry.  With request_timeout=250ms and
// request_retries=1, each read gets two attempts (500ms total), giving
// appliances time to service slow first-access NVRAM lookups or transient
// bus collisions without needing bridge-level retry logic.
static const tiny_gea2_erd_client_configuration_t gea2_client_configuration = {
  .request_timeout = 250,
  .request_retries = 1
};

// Tick-counter time source for the GEA2 interface's internal timer group.
// The counter is incremented once per real millisecond inside the GEA2 tight
// loop so that tiny_gea2_interface's internal timers advance by at most 1 ms
// per event regardless of the ~50 ms ESPHome framework gap between loop() calls
// (see doc/geappliances_bridge.md §13 for the full explanation).
//
// The tick count and last_ms are class members (gea2_tick_count_, gea2_last_ms_)
// so they reset on re-init (deep sleep wake, ESPHome reconfiguration).
struct gea2_tick_source_t {
  i_tiny_time_source_t base;
  GeappliancesBridge* bridge;
};

tiny_time_source_ticks_t gea2_tick_ticks(i_tiny_time_source_t* _self)
{
  auto* src = reinterpret_cast<gea2_tick_source_t*>(_self);
  GeappliancesBridge* br = src->bridge;
  if (br) {
    return br->gea2_tick_count_;
  }
  return 0;
}
static const i_tiny_time_source_api_t kGea2TickApi = { gea2_tick_ticks };
static gea2_tick_source_t s_gea2_tick_source = { .base = { &kGea2TickApi }, .bridge = nullptr };

void GeappliancesBridge::setup() {
  // Reset GEA2 state on re-init (deep sleep wake, ESPHome reconfiguration)
  this->gea2_tick_count_ = 0;
  this->gea2_last_ms_ = 0;
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

  // Initialize timer group
  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

  // Initialize the shared ERD cache before any component uses it.
  erd_cache_init(&this->erd_cache_);
  // Initialize the fixed-capacity set for tracking seen subscription ERDs.
  erd_set_init(&this->custom_erd_subscription_seen_erds_);
  // Initialize GEA3 components if GEA3 UART is configured
  if (this->uart_ != nullptr) {
    esphome_uart_adapter_init(&this->uart_adapter_, &this->timer_group_, this->uart_);

    tiny_gea3_interface_init(
      &this->gea3_interface_,
      &this->uart_adapter_.interface,
      this->client_address_,
      this->send_queue_buffer_,
      sizeof(this->send_queue_buffer_),
      this->receive_buffer_,
      sizeof(this->receive_buffer_),
      false);

    tiny_gea3_erd_client_init(
      &this->erd_client_,
      &this->timer_group_,
      &this->gea3_interface_.interface,
      this->client_queue_buffer_,
      sizeof(this->client_queue_buffer_),
      &client_configuration);

    tiny_event_subscription_init(
      &this->erd_client_activity_subscription_,
      this,
      +[](void* context, const void* args) {
        auto self = reinterpret_cast<GeappliancesBridge*>(context);
        auto activity_args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
        self->handle_erd_client_activity_(activity_args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(&this->erd_client_.interface),
      &this->erd_client_activity_subscription_);
  }

  // Initialize GEA2 components if GEA2 UART is configured
  if (this->gea2_uart_ != nullptr) {
    esphome_uart_adapter_init(&this->gea2_uart_adapter_, &this->timer_group_, this->gea2_uart_);

    // Initialize the GEA2 msec-interrupt event that drives the GEA2 interface's
    // internal timeout counters.  The event is published manually inside the
    // GEA2 tight loop (see loop()) so it only fires when GEA2 is actually
    // in use — keeping the shared timer_group_ free of a 1 ms periodic timer
    // that would starve GEA3/polling-bridge timers when GEA3 is active.
    tiny_event_init(&this->gea2_msec_interrupt_);
    s_gea2_tick_source.bridge = this;

    tiny_gea2_interface_init(
      &this->gea2_interface_,
      &this->gea2_uart_adapter_.interface,
      &s_gea2_tick_source.base,
      &this->gea2_msec_interrupt_.interface,
      this->client_address_,
      this->gea2_send_queue_buffer_,
      sizeof(this->gea2_send_queue_buffer_),
      this->gea2_receive_buffer_,
      sizeof(this->gea2_receive_buffer_),
      false,
      1);

    tiny_gea2_erd_client_init(
      &this->gea2_erd_client_,
      &this->timer_group_,
      &this->gea2_interface_.interface,
      this->gea2_client_queue_buffer_,
      sizeof(this->gea2_client_queue_buffer_),
      &gea2_client_configuration);

    // Initialize the GEA2-to-GEA3 adapter and subscribe to its activity
    gea2_erd_client_adapter_init(&this->gea2_erd_client_adapter_, &this->gea2_erd_client_.interface);

    tiny_event_subscription_init(
      &this->gea2_activity_subscription_,
      this,
      +[](void* context, const void* args) {
        auto self = reinterpret_cast<GeappliancesBridge*>(context);
        auto activity_args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
        self->handle_erd_client_activity_(activity_args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(&this->gea2_erd_client_adapter_.interface),
      &this->gea2_activity_subscription_);
  }

  // Initialize autodiscovery manager (timer-driven, self-driving)
  // Must be called AFTER ERD clients and UART adapters are initialized.
  this->autodiscovery_manager_.init(
      &this->timer_group_,
      this->uart_ != nullptr ? &this->erd_client_.interface : nullptr,
      this->gea2_uart_ != nullptr ? &this->gea2_erd_client_.interface : nullptr,
      this->gea2_uart_ != nullptr ? &this->gea2_erd_client_adapter_.interface : nullptr,
      this->uart_ != nullptr ? &this->uart_adapter_ : nullptr,
      this->gea2_uart_ != nullptr ? &this->gea2_uart_adapter_ : nullptr,
      this->uart_ != nullptr,
      this->gea2_uart_ != nullptr,
      this->client_address_,
      [this]() {
        // Signal the HSM to transition to the device_id phase.
        // The HSM handles DeviceIdentityManager::init() directly.
        tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_autodiscovery_complete, nullptr);
      });

  // Device ID will be set after autodiscovery completes (either configured or autogenerated)
  if (this->configured_device_id_[0] != '\0') {
    ESP_LOGI(TAG, "Device ID configured: %s (will be applied after autodiscovery)", this->configured_device_id_);
  } else {
    ESP_LOGI(TAG, "No device_id configured, will auto-generate after autodiscovery");
  }
  // device_id_state_ stays IDLE until autodiscovery completes

  // The startup HSM handles the boot stabilization delay before autodiscovery.
  ESP_LOGI(TAG, "Waiting %lu seconds before starting autodiscovery...",
           (unsigned long)(AUTODISCOVERY_STARTUP_DELAY_MS / 1000));

  // Initialize OTA cleanup manager with references to needed bridge state.
  this->ota_cleanup_manager_.init(
      &this->ha_discovery_manager_,
      this->device_identity_manager_,
      this->mqtt_client_adapter_,
      &this->erd_cache_,
      this->generate_device_config_,
      this->appliance_api_parsing_,
      this->filter_config_topics_,
      this->steady_state_reached_,
      this->mqtt_client_adapter_initialized_,
      []() { esphome::App.safe_reboot(); });
  // Initialize diagnostic sensor publisher with sensor pointers and cache references.
  this->diagnostic_sensor_publisher_.init(
      this->erd_publish_rate_sensor_,
      this->mqtt_publish_rate_sensor_,
      this->erd_cache_entries_sensor_,
      this->erd_cache_updates_sensor_,
      this->mqtt_disconnect_count_sensor_,
      this->mqtt_disconnect_duration_sensor_,
      this->erd_cache_,
      this->erd_cache_publisher_);

  // NOTE: Discovery change detection (hash + device ID) is now done in
  // check_steady_state() where the device identity is available.

  ESP_LOGCONFIG(TAG, "GE Appliances Bridge setup complete");
}

void GeappliancesBridge::loop() {

  // Drive the GEA2/GEA3 protocol stack FIRST so that UART bytes are
  // processed before any MQTT work.  The tight loop must run before
  // MQTT operations to avoid starving UART processing on single-core
  // ESP32 variants where a blocking MQTT call can delay response
  // processing past the appliance's timeout window.
  this->run_protocol_stack_();

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

  // Initialize the startup HSM on the first loop() call.
  if (this->startup_hsm_wrapper_.hsm.current == nullptr) {
    startup_hsm_wrapper_init(&this->startup_hsm_wrapper_, this, startup_state_startup_delay);
  }

  // Send the run_loop signal to the current HSM state — this drives
  // the ongoing work for whatever phase we're in.
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


  // ── OTA cleanup + discovery refresh (delegated to OtaCleanupManager) ────
  this->ota_cleanup_manager_.loop();

  // ── Diagnostic sensor publishing (delegated to DiagnosticSensorPublisher) ──
  this->diagnostic_sensor_publisher_.loop();
}
// ---------------------------------------------------------------------------
// Publisher pause/resume + steady-state detection
// ---------------------------------------------------------------------------
// Explicit state machine for the ERD cache publisher during HA discovery.
// States:
//   IDLE       — publisher running normally (erd_cache_publisher_paused_ = false)
//   PAUSED     — publisher paused during discovery (erd_cache_publisher_paused_ = true)
//   RESUMING   — just resumed, waiting for first full drain (discovery_just_resumed_ = true)
//
// Transitions:
//   IDLE  -> PAUSED   : ha_discovery_manager_is_processing() returns true
//   PAUSED -> RESUMING : ha_discovery_manager_is_processing() returns false
//   RESUMING -> IDLE   : first_round_done() returns true (steady state reached)

void GeappliancesBridge::update_publisher_state_()
{
  bool ha_discovery_active = ha_discovery_manager_is_processing(&this->ha_discovery_manager_);

  if (ha_discovery_active) {
    // Transition: IDLE -> PAUSED
    if (this->erd_cache_publisher_.cache != nullptr) {
      erd_cache_mqtt_publisher_pause(&this->erd_cache_publisher_);
      if (!this->erd_cache_publisher_paused_) {
        ESP_LOGD(TAG, "ERD cache publisher paused during MQTT discovery payload generation");
        this->erd_cache_publisher_paused_ = true;
      }
    }
  } else {
    // Transition: PAUSED -> RESUMING
    if (this->erd_cache_publisher_.cache != nullptr) {
      erd_cache_mqtt_publisher_resume(&this->erd_cache_publisher_);
      if (this->erd_cache_publisher_paused_) {
        ESP_LOGD(TAG, "ERD cache publisher resumed after MQTT discovery payload generation");
        this->erd_cache_publisher_paused_ = false;
        this->discovery_just_resumed_ = true;
      }
    }
  }

  /* Check steady state BEFORE signaling work — the background task sets
   * first_round_done during its drain, and we want to read it before
   * the next signal_work() wakes it again. */
  if (this->discovery_just_resumed_ &&
      erd_cache_mqtt_publisher_first_round_done(&this->erd_cache_publisher_)) {
    ESP_LOGI(TAG, "Device is in steady state");
    this->discovery_just_resumed_ = false;
  }

  if (this->erd_cache_publisher_.cache != nullptr && !ha_discovery_active) {
    erd_cache_mqtt_publisher_signal_work(&this->erd_cache_publisher_);
  }
}

// ---------------------------------------------------------------------------
// Phase 0: Drive the GEA2/GEA3 hardware stack
// ---------------------------------------------------------------------------

void GeappliancesBridge::run_gea2_iteration_()
{
  if (this->gea2_last_ms_ == 0) {
    this->gea2_last_ms_ = millis();
  }
  uint32_t now_ms = millis();
  uint32_t catchup_count = 0;
  while (this->gea2_last_ms_ < now_ms && catchup_count < MSEC_CATCHUP_CAP) {
    this->gea2_tick_count_++;
    tiny_event_publish(&this->gea2_msec_interrupt_, nullptr);
    this->gea2_last_ms_++;
    catchup_count++;
  }
  tiny_timer_group_run(&this->timer_group_);
  if (this->uart_ != nullptr) {
    tiny_timer_group_run(&this->timer_group_);
  }
  tiny_gea2_interface_run(&this->gea2_interface_);
}

void GeappliancesBridge::run_gea3_iteration_()
{
  tiny_timer_group_run(&this->timer_group_);
  if (this->gea2_uart_ != nullptr) {
    tiny_timer_group_run(&this->timer_group_);
  }
  tiny_gea3_interface_run(&this->gea3_interface_);
}

void GeappliancesBridge::run_timer_only_iteration_()
{
  tiny_timer_group_run(&this->timer_group_);
  if (this->gea2_uart_ != nullptr) {
    tiny_timer_group_run(&this->timer_group_);
  }
}

void GeappliancesBridge::run_protocol_stack_()
{
  // When GEA2 is active (or during GEA2 autodiscovery), run a 100 ms
  // wall-clock busy loop (with a 200 ms hard cap) so the full TX->RX cycle
  // at 19200 baud completes within a single loop() call.
  bool need_gea2_loop = this->gea2_uart_ != nullptr && (
    this->autodiscovery_manager_.is_gea2_protocol() ||
    this->gea2_protocol_active_ ||
    this->autodiscovery_manager_.get_state() == AUTODISCOVERY_GEA2_BROADCAST_PENDING ||
    this->autodiscovery_manager_.get_state() == AUTODISCOVERY_GEA2_BROADCAST_WAITING);

  // When both UARTs are configured, only enable the adapter for the active
  // protocol.  Both adapters register poll timers in the shared timer_group_,
  // so tiny_timer_group_run() fires both poll callbacks on every call.  By
  // disabling the inactive adapter, its poll() returns early without reading
  // bytes or publishing events to an interface that isn't being driven.
  uint32_t protocol_stack_start = esphome::millis();
  if (this->gea2_uart_ != nullptr && this->uart_ != nullptr) {
    esphome_uart_adapter_set_enabled(&this->uart_adapter_, !need_gea2_loop);
    esphome_uart_adapter_set_enabled(&this->gea2_uart_adapter_, need_gea2_loop);
  }

  if (need_gea2_loop) {
    this->run_tight_loop_([this]() { this->run_gea2_iteration_(); },
                           GEA2_LOOP_DURATION_MS, GEA2_LOOP_HARD_CAP_MS, "GEA2");
  } else if (this->uart_ != nullptr) {
    this->run_tight_loop_([this]() { this->run_gea3_iteration_(); },
                           GEA3_LOOP_DURATION_MS, GEA3_LOOP_HARD_CAP_MS, "GEA3");
  } else {
    this->run_timer_only_iteration_();
  }
  uint32_t loop_elapsed = esphome::millis() - protocol_stack_start;
  if (loop_elapsed >= 1000) {
    ESP_LOGW(TAG, "Long run_protocol_stack: %lums (mode=%s, polling=%s)",
             (unsigned long)loop_elapsed, this->mode_ == BRIDGE_MODE_SUBSCRIBE ? "sub" : (this->mode_ == BRIDGE_MODE_AUTO ? "auto" : "poll"),
             this->erd_bridge_initialized_ ? "yes" : "no");
  }
}

// ---------------------------------------------------------------------------
// Debug helper: log polling HSM state transitions (called every loop)
// ---------------------------------------------------------------------------

void GeappliancesBridge::log_poll_state_transitions_()
{
  if (!this->erd_bridge_initialized_) {
    return;
  }

  subscription_state_t sub_state = this->get_subscription_state();

  // Log polling bridge state changes.  The polling bridge is always active
  // when erd_bridge_initialized_ is true (either as the primary bridge in
  // poll mode, or as the custom-ERD polling bridge alongside subscription).
  if (this->polling_bridge_initialized_) {
    polling_state_t poll_state = this->get_polling_state();
    if (poll_state != polling_state_none && poll_state != this->last_logged_poll_state_) {
      this->last_logged_poll_state_ = poll_state;
      ESP_LOGD(TAG, "Polling bridge state: %s", polling_state_name(poll_state));
    }
  }

  // Log subscription bridge state changes.
  if (this->subscription_bridge_initialized_) {
    if (sub_state != subscription_state_none && sub_state != this->last_logged_subscribe_state_) {
      ESP_LOGI(TAG, "Subscription bridge state: %s (ERDs cached: %u)",
               subscription_state_name(sub_state), erd_cache_get_count(&this->erd_cache_));
      this->last_logged_subscribe_state_ = sub_state;
    }
  }
}

void GeappliancesBridge::handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args) {
  // Subscription publications: track ERDs covered by subscription for
  // custom ERD polling bridge filtering.
  if (this->erd_bridge_initialized_ &&
      args->address == this->autodiscovery_manager_.get_host_address() &&
      args->type == tiny_gea3_erd_client_activity_type_subscription_publication_received) {
    erd_set_insert(&this->custom_erd_subscription_seen_erds_, args->subscription_publication_received.erd);
  }

  // Device ID reads (after discovery, before bridge init)
  // Note: FeatureBitManager subscribes directly to ERD client activity events,
  // so the bridge no longer routes feature bit ERDs to it.
  if (!this->erd_bridge_initialized_ && args->address == this->autodiscovery_manager_.get_host_address()) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed) {
      tiny_erd_t erd = args->read_completed.erd;
      const uint8_t* data = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t size = args->read_completed.data_size;
      if (!this->should_route_to_feature_bits_(erd)) {
        this->device_identity_manager_.on_erd_read_completed(erd, data, size);
        if (this->device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE) {
          // Signal the startup HSM that device ID is ready.
          tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_device_id_complete, nullptr);
        }
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
  if (this->polling_bridge_initialized_) {
    polling_state_t poll_state = this->get_polling_state();
    const char* poll_state_str = polling_state_name(poll_state);
    if (poll_state_str != nullptr) {
      ESP_LOGCONFIG(TAG, "  Polling Bridge State: %s", poll_state_str);
    }
  }
  if (this->custom_erds_count_ > 0) {
    ESP_LOGCONFIG(TAG, "  Custom ERDs: %u configured", this->custom_erds_count_);
  }

  // Display current startup state for debugging
  ESP_LOGCONFIG(TAG, "  Startup State: %s", phase_str);
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after MQTT (priority 50), same priority as UART (priority 600)
  return setup_priority::DATA;  // Priority 600
}

bool GeappliancesBridge::teardown() {
  // Reset GEA2 globals so re-init starts fresh
  s_gea2_tick_source.bridge = nullptr;
  this->gea2_tick_count_ = 0;
  this->gea2_last_ms_ = 0;
  // Clear the seen ERDs set for symmetry with erd_set_init() in setup().
  erd_set_clear(&this->custom_erd_subscription_seen_erds_);
  // Reset the feature bit failure log flag for correct behavior after re-init.
  this->feature_bit_failure_logged_ = false;
  // Destroy the startup HSM wrapper (unsubscribes event subscriptions).
  startup_hsm_wrapper_destroy(&this->startup_hsm_wrapper_);
  // Clean up feature bit manager (unsubscribe from ERD client events, stop timers).
  this->feature_bit_manager_.cleanup();
  // Clean up autodiscovery manager (unsubscribe from ERD client events, stop timer).
  this->autodiscovery_manager_.cleanup();
  // Clean up device identity manager (reset state, clear pending reads).
  this->device_identity_manager_.cleanup();
  // Destroy whichever bridge(s) were actually initialized.
  // Using explicit ownership flags makes this unambiguous and prevents
  // double-free or missed cleanup.
  if (this->subscription_bridge_initialized_) {
    erd_bridge_subscribe_destroy(&this->erd_bridge_subscribe_);
    this->subscription_bridge_initialized_ = false;
  }
  if (this->polling_bridge_initialized_) {
    erd_bridge_poll_destroy(&this->erd_bridge_poll_);
    this->polling_bridge_initialized_ = false;
  }
  if (this->write_bridge_initialized_) {
    erd_write_bridge_destroy(&this->erd_write_bridge_);
    this->write_bridge_initialized_ = false;
  }

  // Destroy the shared ERD cache after bridges are torn down.

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
  if (this->mqtt_client_adapter_initialized_) {
    esphome_mqtt_client_adapter_destroy(&this->mqtt_client_adapter_);
    this->mqtt_client_adapter_initialized_ = false;
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

// -- Autodiscovery ------------------------------------------------------------

void GeappliancesBridge::run_autodiscovery()
{
  // Skip autodiscovery if no ERD client is configured or initialized.
  // erd_client_.interface.api is set by tiny_gea3_erd_client_init() in setup();
  // gea2_erd_client_adapter_.interface.api is set by gea2_erd_client_adapter_init().
  bool has_gea3_client = (this->uart_ != nullptr && this->erd_client_.interface.api != nullptr);
  bool has_gea2_client = (this->gea2_uart_ != nullptr && this->gea2_erd_client_adapter_.interface.api != nullptr);
  if (!has_gea3_client && !has_gea2_client) {
    return;
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

// -- Device ID ----------------------------------------------------------------

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

// -- MQTT client adapter ------------------------------------------------------

bool GeappliancesBridge::is_mqtt_client_initialized() const
{
  return mqtt_client_adapter_initialized_;
}

void GeappliancesBridge::initialize_mqtt_client()
{
  initialize_mqtt_client_();
}

// -- Feature bits -------------------------------------------------------------

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

// -- Bridge initialization ----------------------------------------------------

bool GeappliancesBridge::is_bridge_initialized() const
{
  return erd_bridge_initialized_;
}

void GeappliancesBridge::initialize_erd_bridge()
{
  initialize_erd_bridge_();
}

// -- Operating mode -----------------------------------------------------------

BridgeMode GeappliancesBridge::get_mode() const
{
  return mode_;
}

subscription_state_t GeappliancesBridge::get_subscription_state() const
{
  return this->erd_bridge_subscribe_.current_state;
}

polling_state_t GeappliancesBridge::get_polling_state() const
{
  if (!this->polling_bridge_initialized_) {
    return polling_state_none;
  }
  return this->erd_bridge_poll_.current_state;
}
bool GeappliancesBridge::check_steady_state()
{
  // Steady when: not already reached, subscription bridge is steady (if
  // initialized), polling bridge is polling (if initialized), and at least
  // one bridge has been initialized.
  bool steady = !this->steady_state_reached_ &&
    (!this->subscription_bridge_initialized_ ||
     this->erd_bridge_subscribe_.current_state == subscription_state_steady) &&
    (!this->polling_bridge_initialized_ ||
     this->erd_bridge_poll_.current_state == polling_state_polling) &&
    this->erd_bridge_initialized_;

  if (steady) {
    this->steady_state_reached_ = true;
    ESP_LOGI(TAG, "Appliance Bridge is in steady state (ERDs cached: %u, arena: %u/%u bytes, %u%%)",
             erd_cache_get_count(&this->erd_cache_),
             erd_cache_get_arena_usage(&this->erd_cache_),
             ERD_CACHE_ARENA_SIZE,
             erd_cache_get_arena_usage_percent(&this->erd_cache_));

    if (this->generate_device_config_) {
      // Check for discovery changes (hash or device ID) vs. last published state.
      this->ota_cleanup_manager_.check_discovery_changes(
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
    this->device_identity_manager_.get_device_id());

  // Start the background publishing task (ESP-IDF framework only).
  erd_cache_mqtt_publisher_start(&this->erd_cache_publisher_);

  // Initialize the HA discovery manager (lazy-started on steady state).
  ha_discovery_manager_init(&this->ha_discovery_manager_);
}

void GeappliancesBridge::trigger_discovery_refresh()
{
  this->ota_cleanup_manager_.trigger_discovery_refresh();
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
