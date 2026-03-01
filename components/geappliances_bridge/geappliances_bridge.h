#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include <string>

extern "C" {
#include "mqtt_bridge.h"
#include "mqtt_bridge_polling.h"
#include "tiny_gea2_erd_client.h"
#include "tiny_gea2_interface.h"
#include "tiny_gea3_erd_client.h"
#include "tiny_gea3_interface.h"
#include "tiny_timer.h"
}

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

// GEA protocol mode for autodiscovery and device ID generation
// Note: These enum values must match GEA_MODE_*_VALUE constants in __init__.py
enum GEAMode {
  GEA_MODE_AUTO = 0,  // Try GEA3 first, then GEA2
  GEA_MODE_GEA3 = 1,  // Use GEA3 only
  GEA_MODE_GEA2 = 2   // Use GEA2 only
};

class GeappliancesBridge : public Component {
 public:
  static constexpr unsigned long baud = 230400;
  static constexpr uint8_t MAX_BOARDS = 8;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_gea3_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_gea2_uart(uart::UARTComponent *uart) { this->gea2_uart_ = uart; }
  void set_device_id(const std::string &device_id) { this->configured_device_id_ = device_id; }
  void set_mode(uint8_t mode) { this->mode_ = static_cast<BridgeMode>(mode); }
  void set_polling_interval(uint32_t polling_interval) { this->polling_interval_ms_ = polling_interval; }
  void set_polling_only_publish_on_change(bool only_publish_on_change) { this->polling_only_publish_on_change_ = only_publish_on_change; }
  void set_gea3_address(uint8_t address) { this->gea3_address_preference_ = address; }
  void set_gea2_address(uint8_t address) { this->gea2_address_preference_ = address; }
  void set_gea_mode(uint8_t mode) { this->gea_mode_ = static_cast<GEAMode>(mode); }

 protected:
  void on_mqtt_connected_();
  void notify_mqtt_disconnected_();
  void handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args);
  void handle_gea2_erd_client_activity_(const tiny_gea2_erd_client_on_activity_args_t* args);
  void handle_gea3_raw_packet_(const tiny_gea_packet_t* packet);
  void handle_gea2_raw_packet_(const tiny_gea_packet_t* packet);
  void initialize_mqtt_bridge_();
  void check_subscription_activity_();
  void run_autodiscovery_();
  void start_device_id_generation_();
  std::string bytes_to_string_(const uint8_t* data, size_t size);
  std::string bytes_to_hex_string_(const uint8_t* data, size_t size);
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

  enum BridgeInitState {
    BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID,
    BRIDGE_INIT_STATE_WAITING_FOR_MQTT,
    BRIDGE_INIT_STATE_COMPLETE
  };

  enum AutodiscoveryState {
    AUTODISCOVERY_WAITING_FOR_MQTT,          // Waiting for MQTT connection
    AUTODISCOVERY_WAITING_20S,               // MQTT connected, waiting 20 seconds
    AUTODISCOVERY_GEA3_BROADCAST_PENDING,    // About to send GEA3 broadcast
    AUTODISCOVERY_GEA3_BROADCAST_WAITING,    // Sent GEA3 broadcast, waiting 10s for responses
    AUTODISCOVERY_GEA2_BROADCAST_PENDING,    // About to send GEA2 broadcast
    AUTODISCOVERY_GEA2_BROADCAST_WAITING,    // Sent GEA2 broadcast, waiting 10s for responses
    AUTODISCOVERY_COMPLETE                   // At least one board discovered
  };

  uart::UARTComponent *uart_{nullptr};
  uart::UARTComponent *gea2_uart_{nullptr};
  std::string configured_device_id_;
  std::string generated_device_id_;
  std::string final_device_id_;
  uint8_t client_address_{0xE4};
  uint8_t host_address_{0xC0};       // Host address for ERD reads (0xC0 fallback; updated during autodiscovery)
  bool use_gea2_for_device_id_{false}; // Use GEA2 client for device ID reads
  bool mqtt_was_connected_{false};
  bool mqtt_bridge_initialized_{false};
  uint8_t bridge_count_{0};  // Number of initialized bridges (one per discovered board)
  BridgeMode mode_{BRIDGE_MODE_AUTO};
  GEAMode gea_mode_{GEA_MODE_AUTO};
  uint32_t polling_interval_ms_{10000};
  bool polling_only_publish_on_change_{false};
  uint8_t gea3_address_preference_{0xC0}; // Preferred GEA3 board address for device ID generation
  uint8_t gea2_address_preference_{0xA0}; // Preferred GEA2 board address for device ID generation
  
  // Auto mode fallback tracking
  bool subscription_mode_active_{false};
  bool subscription_activity_detected_{false};
  uint32_t subscription_start_time_{0};
  static constexpr uint32_t SUBSCRIPTION_TIMEOUT_MS = 30000; // 30 seconds
  
  DeviceIdState device_id_state_{DEVICE_ID_STATE_IDLE};
  BridgeInitState bridge_init_state_{BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID};

  // Per-board device ID generation
  uint8_t device_id_gen_address_{0xC0}; // Address currently being read for device ID
  uint8_t device_id_board_index_{0};    // Which board in the discovered array we're generating for
  uint8_t total_boards_for_device_id_{0}; // Total boards needing device ID generation
  // Per-board generated device IDs; index i aligns with gea3_discovered_addresses_[i] (or gea2)
  std::string board_device_ids_[MAX_BOARDS];
  // Per-board appliance types; index i aligns with gea3_discovered_addresses_[i] (or gea2)
  uint8_t board_appliance_types_[MAX_BOARDS]{};

  // Autodiscovery state machine
  AutodiscoveryState autodiscovery_state_{AUTODISCOVERY_WAITING_FOR_MQTT};
  uint32_t autodiscovery_timer_start_{0};
  bool gea3_board_discovered_{false};
  bool gea3_preferred_found_{false};
  uint8_t gea3_discovered_addresses_[MAX_BOARDS];  // All GEA3 boards that responded
  uint8_t gea3_discovered_count_{0};               // Number of GEA3 boards discovered
  uint8_t gea3_discovery_poll_count_{0};           // Number of broadcasts sent this cycle
  uint32_t gea3_last_poll_time_{0};                // Timestamp of last GEA3 broadcast
  bool gea2_board_discovered_{false};
  bool gea2_preferred_found_{false};
  uint8_t gea2_discovered_addresses_[MAX_BOARDS];  // All GEA2 boards that responded
  uint8_t gea2_discovered_count_{0};               // Number of GEA2 boards discovered
  uint8_t gea2_discovery_poll_count_{0};           // Number of broadcasts sent this cycle
  uint32_t gea2_last_poll_time_{0};                // Timestamp of last GEA2 broadcast
  static constexpr uint32_t STARTUP_DELAY_MS = 20000;              // 20s after MQTT connects
  static constexpr uint32_t AUTODISCOVERY_BROADCAST_WINDOW_MS = 10000; // 10s window per broadcast cycle
  static constexpr uint8_t  AUTODISCOVERY_POLL_COUNT = 5;           // Repeat broadcast this many times
  static constexpr uint32_t AUTODISCOVERY_REPEAT_INTERVAL_MS = 2000; // Interval between broadcasts

  tiny_gea3_erd_client_request_id_t pending_request_id_;
  tiny_gea2_erd_client_request_id_t gea2_pending_request_id_;
  uint8_t appliance_type_{0};
  std::string model_number_;
  std::string serial_number_;
  uint32_t read_retry_count_{0};
  static constexpr uint32_t LOG_EVERY_N_RETRIES = 50; // Log retry attempts periodically
  static constexpr uint32_t MAX_READ_RETRIES = 1000; // Maximum retries before giving up (about 10 seconds at loop rate)

  tiny_timer_group_t timer_group_;

  // GEA3 components
  esphome_uart_adapter_t uart_adapter_;
  esphome_mqtt_client_adapter_t mqtt_client_adapters_[MAX_BOARDS];

  tiny_gea3_interface_t gea3_interface_;
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[1000];

  tiny_gea3_erd_client_t erd_client_;
  uint8_t client_queue_buffer_[1024];

  // GEA2 components (only used when gea2_uart_ is non-null)
  esphome_uart_adapter_t gea2_uart_adapter_;
  tiny_event_t msec_interrupt_event_;
  tiny_timer_t gea2_msec_timer_;

  tiny_gea2_interface_t gea2_interface_;
  uint8_t gea2_receive_buffer_[255];
  uint8_t gea2_send_queue_buffer_[1000];

  tiny_gea2_erd_client_t gea2_erd_client_;
  uint8_t gea2_client_queue_buffer_[1024];

  mqtt_bridge_t mqtt_bridges_[MAX_BOARDS];
  mqtt_bridge_polling_t mqtt_bridge_pollings_[MAX_BOARDS];

  tiny_event_subscription_t erd_client_activity_subscription_;
  tiny_event_subscription_t gea2_erd_client_activity_subscription_;
  tiny_event_subscription_t gea3_raw_packet_subscription_;
  tiny_event_subscription_t gea2_raw_packet_subscription_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
