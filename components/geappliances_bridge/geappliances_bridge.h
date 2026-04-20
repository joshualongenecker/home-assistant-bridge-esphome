#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include <string>
#include <set>
#include <vector>

extern "C" {
#include "mqtt_bridge.h"
#include "mqtt_bridge_polling.h"
#include "tiny_gea3_erd_client.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea2_erd_client.h"
#include "tiny_gea2_interface.h"
#include "tiny_timer.h"
}

#include "gea2_erd_client_adapter.h"

#include "esphome_uart_adapter.h"
#include "esphome_mqtt_client_adapter.h"

// Forward declaration of the generated function
std::string appliance_type_to_string(uint8_t appliance_type);

namespace esphome {
namespace geappliances_bridge {

// Operation mode for the bridge
// Note: These enum values must match MODE_*_VALUE constants in __init__.py
enum BridgeMode {
  BRIDGE_MODE_POLL = 0,       // Always use polling mode
  BRIDGE_MODE_SUBSCRIBE = 1,  // Always use subscription mode
  BRIDGE_MODE_AUTO = 2        // Auto: try subscription, fallback to polling
};

class GeappliancesBridge : public Component {
 public:
  static constexpr unsigned long baud = 230400;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_gea3_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_gea2_uart(uart::UARTComponent *uart) { this->gea2_uart_ = uart; }
  void set_client_address(uint8_t address) { this->client_address_ = address; }
  void set_device_id(const std::string &device_id) { this->configured_device_id_ = device_id; }
  void set_mode(uint8_t mode) { this->mode_ = static_cast<BridgeMode>(mode); }
  void set_polling_interval(uint32_t polling_interval) { this->polling_interval_ms_ = polling_interval; }
  void set_polling_only_publish_on_change(bool only_publish_on_change) { this->polling_only_publish_on_change_ = only_publish_on_change; }
  void set_appliance_api_parsing(bool appliance_api_parsing) { this->appliance_api_parsing_ = appliance_api_parsing; }
  void set_generate_device_config(bool generate_device_config) { this->generate_device_config_ = generate_device_config; }
  void add_custom_erd(uint16_t erd) { this->custom_erds_vec_.push_back(static_cast<tiny_erd_t>(erd)); }
  void set_ha_discovery_base_url(const std::string& url) { this->ha_discovery_base_url_ = url; }

 protected:
  void on_mqtt_connected_();
  void notify_mqtt_disconnected_();
  void handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args);
  void initialize_mqtt_bridge_();
  void publish_ha_discovery_();
  void publish_next_ha_discovery_entity_();
  void configure_polling_optional_lists_();
  void check_subscription_activity_();
  void run_autodiscovery_();
  void start_feature_bit_reading_();
  void start_device_id_generation_();
  void process_device_id_erd_response_(tiny_erd_t erd, const uint8_t* data, uint8_t size);
  void handle_device_id_read_failure_(tiny_erd_t erd);
  void process_feature_bit_erd_response_(tiny_erd_t erd, const uint8_t* data, uint8_t size);
  void handle_feature_bit_read_failure_(tiny_erd_t erd);
  void skip_to_next_feature_erd_(tiny_erd_t failed_erd);
  void parse_and_log_feature_bits_();
  std::string bytes_to_string_(const uint8_t* data, size_t size);
  std::string sanitize_for_mqtt_topic_(const std::string& input);
  bool try_read_erd_with_retry_(tiny_erd_t erd, const char* erd_name);

  enum DeviceIdState {
    DEVICE_ID_STATE_IDLE,
    DEVICE_ID_STATE_READING_APPLIANCE_TYPE,
    DEVICE_ID_STATE_READING_MODEL_NUMBER,
    DEVICE_ID_STATE_READING_SERIAL_NUMBER,
    DEVICE_ID_STATE_COMPLETE,
    DEVICE_ID_STATE_FAILED
  };

  // States for reading the appliance API feature bit ERDs:
  //   0x0092 (common), 0x0093-0x0097 (appliance groups 0-4),
  //   0x0109-0x010D (appliance groups 5-9).
  // This step runs after autodiscovery and before device ID generation.
  //
  // READING_XXXX: loop() needs to queue a read for that ERD.
  // IN_FLIGHT:    a read has been queued and is waiting for a response; loop()
  //               skips this state so it does not issue a duplicate request.
  //               On response (or failure), the handler transitions back to
  //               the next READING state or to COMPLETE/FAILED.
  enum FeatureBitState {
    FEATURE_BIT_STATE_IDLE,          // Not started yet
    FEATURE_BIT_STATE_READING_0092,  // Need to queue read for ERD 0x0092
    FEATURE_BIT_STATE_READING_0093,  // Need to queue read for ERD 0x0093
    FEATURE_BIT_STATE_READING_0094,  // Need to queue read for ERD 0x0094
    FEATURE_BIT_STATE_READING_0095,  // Need to queue read for ERD 0x0095
    FEATURE_BIT_STATE_READING_0096,  // Need to queue read for ERD 0x0096
    FEATURE_BIT_STATE_READING_0097,  // Need to queue read for ERD 0x0097
    FEATURE_BIT_STATE_READING_0109,  // Need to queue read for ERD 0x0109
    FEATURE_BIT_STATE_READING_010A,  // Need to queue read for ERD 0x010A
    FEATURE_BIT_STATE_READING_010B,  // Need to queue read for ERD 0x010B
    FEATURE_BIT_STATE_READING_010C,  // Need to queue read for ERD 0x010C
    FEATURE_BIT_STATE_READING_010D,  // Need to queue read for ERD 0x010D
    FEATURE_BIT_STATE_IN_FLIGHT,     // Read queued, waiting for response
    FEATURE_BIT_STATE_COMPLETE,
    FEATURE_BIT_STATE_FAILED
  };

  enum BridgeInitState {
    BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID,
    BRIDGE_INIT_STATE_WAITING_FOR_MQTT,
    BRIDGE_INIT_STATE_COMPLETE
  };

  enum AutodiscoveryState {
    AUTODISCOVERY_WAITING_FOR_MQTT,          // Waiting for MQTT connection
    AUTODISCOVERY_WAITING_5S,                // MQTT connected, waiting 5 seconds
    AUTODISCOVERY_GEA3_BROADCAST_PENDING,    // About to send GEA3 broadcast
    AUTODISCOVERY_GEA3_BROADCAST_WAITING,    // Sent GEA3 broadcast, waiting 5s for responses
    AUTODISCOVERY_GEA2_BROADCAST_PENDING,    // About to send GEA2 broadcast
    AUTODISCOVERY_GEA2_BROADCAST_WAITING,    // Sent GEA2 broadcast, waiting 5s for responses
    AUTODISCOVERY_COMPLETE                   // At least one board discovered
  };

  uart::UARTComponent *uart_{nullptr};
  uart::UARTComponent *gea2_uart_{nullptr};
  std::string configured_device_id_;
  std::string generated_device_id_;
  std::string final_device_id_;
  uint8_t client_address_{0xE4};
  uint8_t host_address_{0xC0};       // Host address for ERD reads (0xC0 fallback; updated during autodiscovery)
  bool mqtt_was_connected_{false};
  bool mqtt_bridge_initialized_{false};
  BridgeMode mode_{BRIDGE_MODE_AUTO};
  uint32_t polling_interval_ms_{10000};
  bool polling_only_publish_on_change_{false};
  bool appliance_api_parsing_{true};
  bool generate_device_config_{false};
  // User-configured custom ERDs to poll in addition to the standard list.
  // Populated by add_custom_erd() calls generated from the YAML custom_erds option.
  std::vector<tiny_erd_t> custom_erds_vec_;

  // Auto mode fallback tracking
  bool subscription_mode_active_{false};
  bool subscription_activity_detected_{false};
  uint32_t subscription_start_time_{0};
  static constexpr uint32_t SUBSCRIPTION_TIMEOUT_MS = 30000; // 30 seconds

  // GEA2 tight-loop duration: covers the full TX→RX cycle at 19200 baud
  // (see doc/geappliances_bridge.md section 13 for detailed explanation)
  static constexpr uint32_t GEA2_LOOP_DURATION_MS = 200;
  bool gea2_protocol_active_{false}; // true once a GEA2 appliance is discovered

  DeviceIdState device_id_state_{DEVICE_ID_STATE_IDLE};
  BridgeInitState bridge_init_state_{BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID};

  // Feature bit reading state machine (runs after autodiscovery, before device ID gen)
  FeatureBitState feature_bit_state_{FEATURE_BIT_STATE_IDLE};
  uint8_t feature_bit_erd_0092_[8]{};  // raw bytes from ERD 0x0092 (common features)
  uint8_t feature_bit_erd_0093_[8]{};  // raw bytes from ERD 0x0093 (appliance APIs, group 0)
  uint8_t feature_bit_erd_0094_[8]{};  // raw bytes from ERD 0x0094 (appliance APIs, group 1)
  uint8_t feature_bit_erd_0095_[8]{};  // raw bytes from ERD 0x0095 (appliance APIs, group 2)
  uint8_t feature_bit_erd_0096_[8]{};  // raw bytes from ERD 0x0096 (appliance APIs, group 3)
  uint8_t feature_bit_erd_0097_[8]{};  // raw bytes from ERD 0x0097 (appliance APIs, group 4)
  uint8_t feature_bit_erd_0109_[8]{};  // raw bytes from ERD 0x0109 (appliance APIs, group 5)
  uint8_t feature_bit_erd_010A_[8]{};  // raw bytes from ERD 0x010A (appliance APIs, group 6)
  uint8_t feature_bit_erd_010B_[8]{};  // raw bytes from ERD 0x010B (appliance APIs, group 7)
  uint8_t feature_bit_erd_010C_[8]{};  // raw bytes from ERD 0x010C (appliance APIs, group 8)
  uint8_t feature_bit_erd_010D_[8]{};  // raw bytes from ERD 0x010D (appliance APIs, group 9)
  uint8_t feature_bit_erd_0092_size_{0};
  uint8_t feature_bit_erd_0093_size_{0};
  uint8_t feature_bit_erd_0094_size_{0};
  uint8_t feature_bit_erd_0095_size_{0};
  uint8_t feature_bit_erd_0096_size_{0};
  uint8_t feature_bit_erd_0097_size_{0};
  uint8_t feature_bit_erd_0109_size_{0};
  uint8_t feature_bit_erd_010A_size_{0};
  uint8_t feature_bit_erd_010B_size_{0};
  uint8_t feature_bit_erd_010C_size_{0};
  uint8_t feature_bit_erd_010D_size_{0};
  // Set of valid ERDs built from parsed feature bits; used when appliance_api_parsing_ is true
  std::set<tiny_erd_t> appliance_api_valid_erds_;
  // Sorted vector of the same set, for passing to the polling bridge as a C array
  std::vector<tiny_erd_t> appliance_api_valid_erds_vec_;
  bool appliance_api_valid_list_ready_{false};
  // Flag set by the GEA callback (process_feature_bit_erd_response_ / skip_to_next_feature_erd_)
  // when the last feature-bit ERD has been processed. The actual parsing and transition to
  // device-ID generation are deferred to loop() so the callback returns quickly and the GEA2
  // tight-loop continues processing UART bytes without being stalled by parse_and_log_feature_bits_().
  bool feature_bit_parse_pending_{false};

  // HA device discovery publish: deferred until ERD registration has settled.
  // In subscription mode: publish 10 s after the last NEW ERD subscription
  //   publication is received (or 30 s from bridge init as a safety cap).
  // In polling mode: publish 10 s after the last ERD is registered by the
  //   polling bridge (tracked by comparing ha_registered_erds_.size() each
  //   loop iteration — the same 30 s cap applies).
  bool ha_discovery_pending_{false};
  bool ha_discovery_published_{false};
  bool ha_discovery_publish_in_progress_{false};
  uint32_t ha_discovery_last_activity_{0};         // millis() of last NEW ERD registered/seen (subscription mode)
  uint32_t ha_entity_last_publish_ms_{0};          // millis() of last HA entity publish (rate-limiter)
  const char* last_logged_poll_state_{nullptr};    // tracks current_state_name to detect transitions for debug logging
  // ERD IDs received via subscription that have been seen at least once.
  // The quiet window is only reset when a NEW ERD ID arrives; repeated value
  // updates for already-known ERDs do not extend the wait.
  std::set<tiny_erd_t> ha_discovery_seen_erds_;
  // Set of all ERD IDs that the device has registered (populated by the MQTT
  // adapter's register_erd callback). Used to filter HA discovery entities so
  // only ERDs actually supported by the connected device are published.
  std::set<tiny_erd_t> ha_registered_erds_;
  // Snapshot of ha_registered_erds_ taken just before the HA fetch task is
  // launched. The background task uses this stable copy instead of the live set
  // to avoid a data race: the main loop may insert into ha_registered_erds_
  // (via register_erd callbacks) while the task iterates/queries it.
  std::set<tiny_erd_t> ha_registered_erds_snapshot_;
  // Set of string-type ERD IDs built from ha_discovery_config.h at bridge init.
  // Passed to the MQTT adapter so it can publish ASCII text instead of hex.
  std::set<tiny_erd_t> ha_string_erds_set_;
  static constexpr uint32_t HA_DISCOVERY_QUIET_MS = 10000;  // 10 s quiet period (subscription mode)
  // Minimum interval between successive HA entity publishes. Publishing one QoS-1
  // MQTT message per loop() call (which runs ~100s of times/sec) floods the IDF
  // MQTT event queue and causes "Dropped inbound MQTT events" warnings. 50 ms
  // gives the stack time to send the packet and process the PUBACK before the next
  // one arrives while keeping total discovery time reasonable (<100 entities × 50 ms = 5 s).
  static constexpr uint32_t HA_ENTITY_PUBLISH_INTERVAL_MS = 50;

  // --- Runtime HA-discovery fetch state ------------------------------------
  // Entity definitions are downloaded at runtime from compact JSONL files
  // rather than stored in flash.  A FreeRTOS background task performs the
  // HTTPS fetch; each discovered entity is sent to a queue that the main
  // loop() drains one publish per iteration (preserving the existing
  // rate-limiting behaviour).

  // A (topic, payload) pair ready to be published via MQTT.
  struct HaDiscoveryItem {
    std::string topic;
    std::string payload;
  };

  // Base URL for the per-category JSONL files.
  // Can be overridden in YAML via ha_discovery_base_url.
  std::string ha_discovery_base_url_{
    "https://raw.githubusercontent.com/joshualongenecker/"
    "home-assistant-bridge-esphome/main/ha_discovery"
  };

  QueueHandle_t ha_discovery_queue_{nullptr};   // carries HaDiscoveryItem* (nullptr = sentinel)
  TaskHandle_t  ha_fetch_task_handle_{nullptr};

  static void ha_fetch_task_fn_(void* param);
  void        fetch_ha_definitions_();
  bool        fetch_category_(const std::string& url,
                              const std::string& device_id,
                              const std::string& device_json);
  bool        process_jsonl_line_(const char* line,
                                  const std::string& device_id,
                                  const std::string& device_json);


  // Autodiscovery state machine
  AutodiscoveryState autodiscovery_state_{AUTODISCOVERY_WAITING_FOR_MQTT};
  uint32_t autodiscovery_timer_start_{0};
  bool gea3_board_discovered_{false};
  bool gea2_board_discovered_{false};
  static constexpr uint32_t STARTUP_DELAY_MS = 5000;               // 5s after MQTT connects
  static constexpr uint32_t AUTODISCOVERY_BROADCAST_WINDOW_MS = 5000;  // 5s window per broadcast

  tiny_gea3_erd_client_request_id_t pending_request_id_;
  uint8_t appliance_type_{0};
  std::string model_number_;
  std::string serial_number_;
  uint32_t read_retry_count_{0};
  static constexpr uint32_t LOG_EVERY_N_RETRIES = 50; // Log retry attempts periodically
  static constexpr uint32_t MAX_READ_RETRIES = 1000; // Maximum retries before giving up (about 10 seconds at loop rate)
  // Counts consecutive READ-RESPONSE failures for the current device-ID ERD.
  // Separate from read_retry_count_ which counts queue failures.  When this
  // reaches MAX_DEVICE_ID_RESPONSE_RETRIES the ERD is skipped and a fallback
  // value is used so device ID generation can always complete.
  uint32_t device_id_response_retries_{0};
  static constexpr uint32_t MAX_DEVICE_ID_RESPONSE_RETRIES = 3;

  tiny_timer_group_t timer_group_;

  // GEA3 components
  esphome_uart_adapter_t uart_adapter_;
  esphome_mqtt_client_adapter_t mqtt_client_adapter_;

  tiny_gea3_interface_t gea3_interface_;
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[1000];

  tiny_gea3_erd_client_t erd_client_;
  uint8_t client_queue_buffer_[1024];

  // GEA2 components (only used when gea2_uart_ is set)
  esphome_uart_adapter_t gea2_uart_adapter_;

  tiny_gea2_interface_t gea2_interface_;
  uint8_t gea2_receive_buffer_[255];
  uint8_t gea2_send_queue_buffer_[10000];

  tiny_gea2_erd_client_t gea2_erd_client_;
  uint8_t gea2_client_queue_buffer_[8096];

  // Event fired once per millisecond to drive GEA2 interface's internal timers.
  // Published manually inside the GEA2 tight loop (not via a timer_group_ periodic
  // timer) so the 1 ms interrupt never fires in the GEA3 single-pass path and
  // cannot starve the GEA3/polling-bridge timers in the shared timer_group_.
  tiny_event_t gea2_msec_interrupt_;

  // Adapter that wraps the GEA2 ERD client as a GEA3 ERD client interface
  gea2_erd_client_adapter_t gea2_erd_client_adapter_;

  i_tiny_gea3_erd_client_t* active_erd_client_{nullptr}; // set during initialize_mqtt_bridge_()

  mqtt_bridge_t mqtt_bridge_;
  mqtt_bridge_polling_t mqtt_bridge_polling_;
  // Polling bridge used exclusively for custom ERDs when the primary bridge is
  // in subscribe (or auto-subscribe) mode. Initialized alongside mqtt_bridge_
  // when custom_erds_vec_ is non-empty and use_polling is false.
  mqtt_bridge_polling_t custom_erd_bridge_;
  bool custom_erd_polling_active_{false};

  tiny_event_subscription_t erd_client_activity_subscription_;
  tiny_event_subscription_t gea2_activity_subscription_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
