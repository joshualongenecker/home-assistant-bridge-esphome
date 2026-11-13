// =============================================================================
// C/C++ CONVENTION
// =============================================================================
// C structs + vtables: data-path components (erd_cache, bridges, interfaces,
//   publishers, discovery). Portable, testable, no C++ overhead.
// C++ classes: ESPHome integration layer (GeappliancesBridge, managers,
//   adapters). Used only where ESPHome APIs or C++ features are needed.
// =============================================================================

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/button/button.h"
#include "esphome/core/application.h"
#include <string>
#include <cstring>
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#ifdef USE_ESP32
#include "esp_task_wdt.h"
#endif

extern "C" {
#include "erd_cache.h"
#include "erd_write_bridge.h"
#include "tiny_gea3_erd_client.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea2_erd_client.h"
#include "tiny_gea2_interface.h"
#include "tiny_timer.h"
#include "tiny_hsm.h"
#include "erd_cache_mqtt_publisher.h"
#include "ha_discovery_manager.h"
#include "ha_discovery_cleanup.h"
}

#include "erd_bridge_subscribe.h"
#include "erd_bridge_poll.h"

#include "gea2_erd_client_adapter.h"

#include "esphome_uart_adapter.h"
#include "bridge_mode.h"
#include "i_bridge_services.h"
#include "erd_registry.h"
#include "esphome_mqtt_client_adapter.h"
#include "device_identity_manager.h"
#include "feature_bit_manager.h"
#include "autodiscovery_manager.h"
#include "geappliances_bridge_startup_hsm.h"
#include "discovery_lifecycle_manager.h"
#include "diagnostic_sensor_publisher.h"
#include "erd_poll_list_builder.h"
#include "protocol_stack.h"

namespace esphome {
namespace geappliances_bridge {

class GeappliancesBridge : public Component, public IBridgeServices {
  friend ErdPollListResult build_poll_list_(GeappliancesBridge* bridge,
                                            const probe_entry_t* custom_erds,
                                            uint16_t custom_erds_count);

 public:
  static constexpr unsigned long baud = 230400;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  bool teardown() override;
  ~GeappliancesBridge() override;

  void set_gea3_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_gea2_uart(uart::UARTComponent *uart) { this->gea2_uart_ = uart; }
  void set_client_address(uint8_t address) { this->client_address_ = address; }
  void set_board_address(uint8_t address) { this->board_address_ = address; this->board_address_configured_ = true; }
  void set_device_id(const std::string &device_id) { strncpy(this->configured_device_id_, device_id.c_str(), sizeof(this->configured_device_id_) - 1); this->configured_device_id_[sizeof(this->configured_device_id_) - 1] = '\0'; }
  void set_mode(uint8_t mode) { this->mode_ = static_cast<BridgeMode>(mode); }
  void set_polling_interval(uint32_t polling_interval) { this->polling_interval_ms_ = polling_interval; }
  void set_appliance_api_parsing(bool appliance_api_parsing) { this->appliance_api_parsing_ = appliance_api_parsing; }
  void set_generate_device_config(bool generate_device_config) { this->generate_device_config_ = generate_device_config; }
  void set_filter_config_topics(bool filter_config_topics) { this->filter_config_topics_ = filter_config_topics; }
  void set_erd_publish_rate_sensor(sensor::Sensor* sensor) { this->erd_publish_rate_sensor_ = sensor; this->diagnostic_sensor_publisher_.set_erd_publish_rate_sensor(sensor); }
  void set_erd_cache_entries_sensor(sensor::Sensor* sensor) { this->erd_cache_entries_sensor_ = sensor; this->diagnostic_sensor_publisher_.set_erd_cache_entries_sensor(sensor); }
  void set_erd_cache_updates_sensor(sensor::Sensor* sensor) { this->erd_cache_updates_sensor_ = sensor; this->diagnostic_sensor_publisher_.set_erd_cache_updates_sensor(sensor); }
  void set_mqtt_publish_rate_sensor(sensor::Sensor* sensor) { this->mqtt_publish_rate_sensor_ = sensor; this->diagnostic_sensor_publisher_.set_mqtt_publish_rate_sensor(sensor); }
  void set_mqtt_disconnect_count_sensor(sensor::Sensor* sensor) { this->mqtt_disconnect_count_sensor_ = sensor; this->diagnostic_sensor_publisher_.set_mqtt_disconnect_count_sensor(sensor); }
  void set_mqtt_disconnect_duration_sensor(sensor::Sensor* sensor) { this->mqtt_disconnect_duration_sensor_ = sensor; this->diagnostic_sensor_publisher_.set_mqtt_disconnect_duration_sensor(sensor); }
  void set_throttle_rate_seconds(uint8_t rate) { this->throttle_rate_seconds_ = rate; }
  void set_custom_ha_discovery_data(const uint8_t* data, const void* chunks, uint16_t count, uint16_t max_chunk, uint32_t hash) {
    ha_discovery_manager_set_custom_data(&this->ha_discovery_manager_, data, chunks, count, max_chunk, hash);
  }
  void add_custom_erds(const uint16_t* erds, uint16_t count);
  void add_custom_erds_with_addresses(const uint16_t* erds, const uint8_t* addresses, uint16_t count);
  void add_custom_erd(tiny_erd_t erd);
  void trigger_discovery_refresh();

 protected:
  // ── IBridgeServices implementation (called exclusively by the startup HSM) ──
  void run_autodiscovery() override;
  bool is_autodiscovery_complete() const override;
  uint8_t get_discovered_host_address() const override;
  bool is_discovered_gea2_protocol() const override;

  void init_device_id_reading() override;
  bool is_device_id_complete() const override;

  bool is_mqtt_client_initialized() const override;
  void initialize_mqtt_client() override;

  void start_feature_bit_reading() override;
  bool is_feature_bits_complete() const override;

  void record_startup_delay_start() override;
  bool is_startup_delay_elapsed() const override;

  bool is_bridge_initialized() const override;
  void initialize_erd_bridge() override;

  BridgeMode get_mode() const override;
  subscription_state_t get_subscription_state() const override;
  polling_state_t get_polling_state() const override;
  void handle_subscription_failed() override;
  void handle_polling_failed() override;
  void maybe_start_custom_erd_polling() override;
  bool check_steady_state() override;
  void log_poll_state_transitions() override;
  void initialize_erd_cache_publisher() override;
  bool is_erd_cache_publisher_initialized() const override;
  void handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args);
  void initialize_mqtt_client_();
  void initialize_erd_bridge_();
  void start_custom_erd_polling_();
  void maybe_start_custom_erd_polling_();
  // ── Data-plane phase machine (replaces the five *_initialized_ booleans) ──
  //
  // kIdle                       no data-plane bridge exists yet
  // kPolling                    polling bridge only (poll mode or fallback)
  // kSubscription               subscription bridge only; custom polling undecided
  // kSubscriptionCustomPolling  subscription bridge + custom-ERD polling bridge
  // kSubscriptionSettled        subscription bridge only; all custom ERDs
  //                             covered, custom polling retired
  //
  // The phase never returns to kIdle: the write bridge is initialized at the
  // end of initialize_erd_bridge_() on every path and destroyed only in
  // teardown().
  enum class DataPlanePhase {
    kIdle,
    kPolling,
    kSubscription,
    kSubscriptionCustomPolling,
    kSubscriptionSettled,
  };
  // One-way readiness latch: true once the appliance-side data path has
  // reached steady-state operation. Kept separate from the phase because a
  // failure rebuild returns the phase to kPolling while the latch stays set.
  enum class DataPlaneReadiness { kNotSteady, kSteady };
  // Publisher pause machine: the ERD cache publisher pauses while HA
  // discovery publishes and resumes afterwards; kResuming tracks the
  // post-resume drain so "Device is in steady state" logs exactly once.
  enum class PublisherPausePhase { kIdle, kPaused, kResuming };

  // Whether the phase implies a live subscription/polling bridge. The
  // subscription bridge survives a custom-polling failure and the settled
  // state; the polling bridge exists only in the two polling phases.
  static bool phase_has_subscription_bridge(DataPlanePhase phase) {
    return phase == DataPlanePhase::kSubscription ||
           phase == DataPlanePhase::kSubscriptionCustomPolling ||
           phase == DataPlanePhase::kSubscriptionSettled;
  }
  static bool phase_has_polling_bridge(DataPlanePhase phase) {
    return phase == DataPlanePhase::kPolling ||
           phase == DataPlanePhase::kSubscriptionCustomPolling;
  }
  void log_poll_state_transitions_();
  void update_publisher_state_();
  // Readiness predicate handed to DiscoveryLifecycleManager: the bridge owns
  // steady state and MQTT initialization, so it answers instead of exposing
  // those members as references the manager could alias.
  static bool discovery_is_ready_(void* context);
  void start_feature_bit_reading_();
  void init_erd_cache_publisher_();
  void init_polling_bridge_(bool log_as_info, const probe_entry_t* custom_erds = nullptr,
                            uint16_t custom_erds_count = 0);
  void on_poll_discovery_complete_();
  bool should_route_to_feature_bits_(tiny_erd_t erd);

  startup_hsm_wrapper_t startup_hsm_wrapper_;
  // ── Data-plane phase machine state ──
  DataPlanePhase data_plane_phase_{DataPlanePhase::kIdle};
  DataPlaneReadiness data_plane_readiness_{DataPlaneReadiness::kNotSteady};
  PublisherPausePhase publisher_pause_phase_{PublisherPausePhase::kIdle};

  // Wire-level protocol machinery: UART adapters, GEA2/GEA3 interfaces, both
  // ERD clients, the GEA2→GEA3 adapter, their buffers, the GEA2 tick source,
  // and the shared timer group (see protocol_stack.h).
  ProtocolStack protocol_stack_;

  uart::UARTComponent *uart_{nullptr};
  uart::UARTComponent *gea2_uart_{nullptr};
  char configured_device_id_[92]{0};
  uint8_t client_address_{0xE4};
  uint8_t board_address_{0};
  bool board_address_configured_{false};

  BridgeMode mode_{BRIDGE_MODE_AUTO};
  uint32_t polling_interval_ms_{10000};
  bool appliance_api_parsing_{true};
  bool generate_device_config_{true};
  bool filter_config_topics_{true};
  uint8_t throttle_rate_seconds_{1};
  uint32_t last_cooldown_tick_{0};
  static constexpr uint16_t CUSTOM_ERDS_MAX = 128;
  struct custom_erd_entry_t {
    tiny_erd_t erd;
    uint8_t board_address;
  };
  custom_erd_entry_t custom_erds_[CUSTOM_ERDS_MAX];
  uint16_t custom_erds_count_{0};
  // Retain the address with the ERD when subscription mode falls back to
  // polling. A bare ERD ID would silently route every fallback read to the
  // primary board.
  probe_entry_t subscription_unseen_custom_erds_[CUSTOM_ERDS_MAX];
  uint16_t subscription_unseen_custom_erds_count_{0};

  erd_set_t custom_erd_subscription_seen_erds_;
  probe_entry_t poll_probe_list_[POLLING_LIST_MAX_SIZE];
  uint16_t poll_probe_list_count_{0};

  uint32_t startup_delay_start_ms_{0};

  DeviceIdentityManager device_identity_manager_;

  FeatureBitManager feature_bit_manager_;

  polling_state_t last_logged_poll_state_{polling_state_none};
  subscription_state_t last_logged_subscribe_state_{subscription_state_none};
  mutable bool feature_bit_failure_logged_{false};

  sensor::Sensor* erd_publish_rate_sensor_{nullptr};

  sensor::Sensor* erd_cache_entries_sensor_{nullptr};
  sensor::Sensor* erd_cache_updates_sensor_{nullptr};
  sensor::Sensor* mqtt_publish_rate_sensor_{nullptr};
  sensor::Sensor* mqtt_disconnect_count_sensor_{nullptr};
  sensor::Sensor* mqtt_disconnect_duration_sensor_{nullptr};

  ErdRegistry erd_registry_;

  erd_cache_mqtt_publisher_t erd_cache_publisher_;
  erd_cache_t erd_cache_;

  // HA discovery manager: publishes one-shot HA MQTT discovery payloads after steady state is reached.
  ha_discovery_manager_t ha_discovery_manager_{};  // zero-init: teardown() frees run buffers pre-setup

  AutodiscoveryManager autodiscovery_manager_;
  // HA discovery lifecycle: initial publish, cleanup + republish (OTA, config
  // change, Discovery Refresh) and the reboot that follows a successful
  // publish. Driven from loop() once steady state is reached.
  DiscoveryLifecycleManager discovery_lifecycle_manager_;
  DiagnosticSensorPublisher diagnostic_sensor_publisher_;

  esphome_mqtt_client_adapter_t mqtt_client_adapter_{};  // zero-init: .initialized reads false pre-setup

  erd_bridge_subscribe_t erd_bridge_subscribe_;
  erd_bridge_poll_t erd_bridge_poll_;

  erd_write_bridge_t erd_write_bridge_;

  tiny_event_subscription_t erd_client_activity_subscription_;
  tiny_event_subscription_t gea2_activity_subscription_;
};

class DiscoveryRefreshButton : public button::Button {
 public:
  DiscoveryRefreshButton(GeappliancesBridge* bridge) : bridge_(bridge) {}

  void press_action() override {
    if (bridge_ != nullptr) {
      bridge_->trigger_discovery_refresh();
    }
  }

 private:
  GeappliancesBridge* bridge_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
