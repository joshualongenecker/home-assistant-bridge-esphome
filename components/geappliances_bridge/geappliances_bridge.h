// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Coordinate the lifecycle of all bridge components and serve as the
//       ESPHome component entry point (setup / loop / dump_config).
//
// Responsibilities:
//   - Own and construct all component instances (adapters, managers, bridges)
//   - Wire components together during setup()
//   - Drive the GEA2 tight-loop and delegate ongoing work in loop()
//   - Expose configuration setters called by the ESPHome code generator
//   - Implement IBridgeServices so the startup HSM can request bridge actions
//     without depending on this concrete class
//
// NOT responsible for:
//   - Assembling the device ID (DeviceIdentityManager)
//   - Determining which ERDs are valid (FeatureBitManager / ErdRegistry)
//   - MQTT connection lifecycle (EsphomeMqttClientAdapter)
//   - Startup phase sequencing (StartupHsm)
//
// Dependencies:
//   - ESPHome UART, MQTT, and Component APIs
//   - tiny_gea3_interface, tiny_gea2_interface, tiny_gea3_erd_client
//   - All manager and adapter classes in this component
// =============================================================================
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
#include "ota_cleanup_manager.h"
#include "diagnostic_sensor_publisher.h"
#include "erd_poll_list_builder.h"
#include "appliance_type_map.h"

namespace esphome {
namespace geappliances_bridge {

// BridgeMode is now defined in bridge_mode.h (included via i_bridge_services.h).



class GeappliancesBridge : public Component, public IBridgeServices {
  friend ErdPollListResult build_poll_list_(GeappliancesBridge* bridge,
                                            const uint16_t* custom_erds,
                                            uint16_t custom_erds_count);
  friend tiny_time_source_ticks_t gea2_tick_ticks(i_tiny_time_source_t*);

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
  // ── Internal bridge methods (event callbacks and per-phase helpers) ─────────
  void handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args);
  void initialize_mqtt_client_();
  void initialize_erd_bridge_();
  void start_custom_erd_polling_();
  void maybe_start_custom_erd_polling_();
  // Protocol stack iteration helpers (extracted from run_protocol_stack_)
  void run_gea2_iteration_();
  void run_gea3_iteration_();
  void run_timer_only_iteration_();
  // Run a tight-loop for the given iteration function, bounded by a duration
  // and a hard cap.  In test builds the loop is replaced with a single call.
  template<typename IterFn>
  void run_tight_loop_(IterFn iter_fn, uint32_t duration_ms,
                        uint32_t hard_cap_ms, const char* protocol_name) {
#ifndef UNIT_TEST_BUILD
    uint32_t loop_start_ms = millis();
    while (millis() - loop_start_ms < duration_ms) {
      if (millis() - loop_start_ms >= hard_cap_ms) {
        ESP_LOGW("geappliances_bridge", "%s tight loop exceeded hard cap (%u ms), breaking",
                 protocol_name, static_cast<unsigned>(hard_cap_ms));
        break;
      }
#ifdef USE_ESP32
      esp_task_wdt_reset();
#endif
      iter_fn();
    }
#else
    (void)duration_ms;
    (void)hard_cap_ms;
    (void)protocol_name;
    iter_fn();
#endif
  }
  void run_protocol_stack_();         // Drive GEA2/GEA3 hardware stack
  void log_poll_state_transitions_(); // Debug: log polling HSM state changes
  void update_publisher_state_();       // Publisher pause/resume + steady-state detection
  void start_feature_bit_reading_();
  void init_erd_cache_publisher_();
  void init_polling_bridge_(bool log_as_info, const uint16_t* custom_erds = nullptr,
                            uint16_t custom_erds_count = 0);
  void on_poll_discovery_complete_();
  bool should_route_to_feature_bits_(tiny_erd_t erd);

  // Startup HSM — replaces the manual switch-based phase progression.
  //   startup_delay → autodiscovery → device_id → mqtt_client_init
  //                 → feature_bits → bridge_init → subscription_watch
  //                 → running
  startup_hsm_wrapper_t startup_hsm_wrapper_;

  uart::UARTComponent *uart_{nullptr};
  uart::UARTComponent *gea2_uart_{nullptr};
  char configured_device_id_[92]{0};
  uint8_t client_address_{0xE4};

  bool mqtt_client_adapter_initialized_{false};
  bool erd_bridge_initialized_{false};
  BridgeMode mode_{BRIDGE_MODE_AUTO};
  uint32_t polling_interval_ms_{10000};
  bool appliance_api_parsing_{true};
  bool generate_device_config_{true};
  bool filter_config_topics_{true};
  uint8_t throttle_rate_seconds_{1};
  uint32_t last_cooldown_tick_{0};  /* last time erd_cache_tick_cooldowns ran (ms) */
  // User-configured custom ERDs to poll in addition to the standard list.
  // Populated by add_custom_erd() calls generated from the YAML custom_erds option.
  // Fixed-capacity array to avoid heap allocation.
  static constexpr uint16_t CUSTOM_ERDS_MAX = 128;
  tiny_erd_t custom_erds_[CUSTOM_ERDS_MAX];
  uint16_t custom_erds_count_{0};
  uint16_t subscription_unseen_custom_erds_[CUSTOM_ERDS_MAX];
  uint16_t subscription_unseen_custom_erds_count_{0};

  // Fixed-capacity set for tracking seen subscription ERDs (replaces std::set).
  erd_set_t custom_erd_subscription_seen_erds_;
  // Pre-built ERD probe list for the polling bridge.
  // Fixed-capacity array to avoid heap allocation.
  uint16_t poll_probe_list_[POLLING_LIST_MAX_SIZE];
  uint16_t poll_probe_list_count_{0};
  bool custom_erd_polling_started_{false};  // Guard to prevent re-initialization

  // Startup phase delay tracking
  uint32_t startup_delay_start_ms_{0};

  // GEA2 tight-loop duration: covers the full TX→RX cycle at 19200 baud
  // (see doc/geappliances_bridge.md section 13 for detailed explanation)
  static constexpr uint32_t GEA2_LOOP_DURATION_MS = 100;
  // Cap on msec catchup iterations to prevent runaway loops
  static constexpr uint32_t MSEC_CATCHUP_CAP = 1000;
  // GEA3 tight-loop duration
  static constexpr uint32_t GEA3_LOOP_DURATION_MS = 10;
  static constexpr uint32_t GEA2_LOOP_HARD_CAP_MS = GEA2_LOOP_DURATION_MS * 2;
  static constexpr uint32_t GEA3_LOOP_HARD_CAP_MS = GEA3_LOOP_DURATION_MS * 2;
  bool gea2_protocol_active_{false}; // fallback for manual device_id when autodiscovery is skipped

  // True once the appliance-side data path (subscription and/or polling
  // bridge) has reached steady-state operation.  Set once; never cleared.
  bool steady_state_reached_{false};

  // Device identity manager (extracted from god class)
  DeviceIdentityManager device_identity_manager_;

  // Feature bit manager (extracted from god class)
  FeatureBitManager feature_bit_manager_;

  // Guard to prevent re-initializing feature bit reading mid-sequence.
  // start_feature_bit_reading_() checks this flag so it won't re-init()
  // if called again while the first ERD read is still in-flight.
  bool feature_bit_reading_started_{false};

  // Feature bit reading state machine (runs after autodiscovery, before device ID gen)
  // The FeatureBitManager owns the valid ERD list and ready flag; use its getters directly.

  polling_state_t last_logged_poll_state_{polling_state_none};
  subscription_state_t last_logged_subscribe_state_{subscription_state_none};
  mutable bool feature_bit_failure_logged_{false};

  // ERD publish rate sensor: counts ERD updates per ~60s window and
  // publishes to Home Assistant.
  sensor::Sensor* erd_publish_rate_sensor_{nullptr};

  sensor::Sensor* erd_cache_entries_sensor_{nullptr};
  sensor::Sensor* erd_cache_updates_sensor_{nullptr};
  sensor::Sensor* mqtt_publish_rate_sensor_{nullptr};
  sensor::Sensor* mqtt_disconnect_count_sensor_{nullptr};
  sensor::Sensor* mqtt_disconnect_duration_sensor_{nullptr};

  // ERD registry: single owner of valid-ERD filter, string-type set,
  // and runtime registered-ERD tracking.
  ErdRegistry erd_registry_;

  // Shared ERD cache — owned by the bridge, used by both bridge HSMs and the
  // MQTT publisher. Entries are updated by the bridges on read/subscription;
  // the publisher drains update_required entries to MQTT each loop().

  // ERD cache MQTT publisher: drains update_required entries from the shared
  // cache and publishes them to MQTT topics each loop().
  erd_cache_mqtt_publisher_t erd_cache_publisher_;
  erd_cache_t erd_cache_;

  // HA discovery manager: publishes one-shot HA MQTT discovery payloads
  // after steady state is reached.
  ha_discovery_manager_t ha_discovery_manager_;
  bool erd_cache_publisher_paused_{false};
  bool discovery_just_resumed_{false};

  // OTA-triggered discovery cleanup: set in setup() if the stored
  // discovery data hash differs from the current build's hash. Driven
  // in loop() after steady state. After cleanup, reboots (same as
  // DiscoveryRefresh) for fresh republish.

  // Autodiscovery manager (extracted from god class)
  AutodiscoveryManager autodiscovery_manager_;
  // OTA cleanup manager: owns the OTA-triggered discovery cleanup state
  // machine and the DiscoveryRefresh path (cleanup → republish → reboot).
  OtaCleanupManager ota_cleanup_manager_;
  // Diagnostic sensor publisher: owns periodic publishing of ERD/MQTT
  // diagnostic sensors (publish rate, cache stats, disconnect stats).
  DiagnosticSensorPublisher diagnostic_sensor_publisher_;


  tiny_timer_group_t timer_group_;

  // GEA3 components
  esphome_uart_adapter_t uart_adapter_;
  esphome_mqtt_client_adapter_t mqtt_client_adapter_;

  tiny_gea3_interface_t gea3_interface_;
  // GEA3 receive buffer — one complete on-wire packet.
  // Max payload is tiny_gea_packet_max_payload_length (248) + 7 bytes overhead = 255.
  uint8_t receive_buffer_[255];
  // GEA3 send queue — holds up to ~4 pending outbound packets (255 bytes each).
  uint8_t send_queue_buffer_[1000];

  tiny_gea3_erd_client_t erd_client_;
  /* GEA3 client queue buffer — sized to hold enough read requests for the
   * custom-ERD polling bridge (up to ~31 reads × 6 bytes each ≈ 188 bytes)
   * plus in-flight subscription acknowledgments and write requests.
   * Increased from 1024 to 2048 to prevent ring-buffer overflow when the
   * polling bridge and subscription bridge share the same ERD client;
   * overflow corrupts adjacent heap metadata causing
   * prvCheckTasksWaitingTermination crashes (see erd_bridge_poll.cpp). */
  uint8_t client_queue_buffer_[4096];

  // GEA2 components (only used when gea2_uart_ is set)
  esphome_uart_adapter_t gea2_uart_adapter_;
  tiny_gea2_interface_t gea2_interface_;

  // GEA2 receive buffer — one complete on-wire packet.
  // Max payload is tiny_gea_packet_max_payload_length (248) + 7 bytes overhead = 255.
  uint8_t gea2_receive_buffer_[255];
  // GEA2 send queue — larger than GEA3 to absorb more packets at 19200 baud
  // where the slower bus means the tight loop processes fewer packets per call.
  uint8_t gea2_send_queue_buffer_[4096];

  tiny_gea2_erd_client_t gea2_erd_client_;
  // GEA2 client queue — same sizing as GEA3 client queue; see comment above.
  uint8_t gea2_client_queue_buffer_[4096];

  // Event fired once per millisecond to drive GEA2 interface's internal timers.
  // Published manually inside the GEA2 tight loop (not via a timer_group_ periodic
  // timer) so the 1 ms interrupt never fires in the GEA3 single-pass path and
  // cannot starve the GEA3/polling-bridge timers in the shared timer_group_.
  tiny_event_t gea2_msec_interrupt_;
  // GEA2 time tracking — moved from file-scope statics to class members
  // so they reset on re-init (deep sleep wake, ESPHome reconfiguration).
  tiny_time_source_ticks_t gea2_tick_count_{0};
  uint32_t gea2_last_ms_{0};

  // Adapter that wraps the GEA2 ERD client as a GEA3 ERD client interface
  gea2_erd_client_adapter_t gea2_erd_client_adapter_;

  erd_bridge_subscribe_t erd_bridge_subscribe_;
  erd_bridge_poll_t erd_bridge_poll_;

  // Write bridge: relays MQTT write requests to the ERD client
  erd_write_bridge_t erd_write_bridge_;
  bool write_bridge_initialized_{false};

  // Track which bridge(s) were actually initialized so teardown is unambiguous.
  // A subscription bridge (erd_bridge_subscribe_) is created when use_polling is false.
  // A polling bridge (erd_bridge_poll_) is created when use_polling is true,
  // or when custom ERD polling is started alongside a subscription bridge.
  bool subscription_bridge_initialized_{false};
  bool polling_bridge_initialized_{false};

  tiny_event_subscription_t erd_client_activity_subscription_;
  tiny_event_subscription_t gea2_activity_subscription_;
};

// DiscoveryRefreshButton: concrete button that triggers HA discovery cleanup
// and device restart when pressed.
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
