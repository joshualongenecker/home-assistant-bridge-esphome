#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include <string>

extern "C" {
#include "mqtt_bridge.h"
#include "mqtt_bridge_polling.h"
#include "tiny_gea3_erd_client.h"
#include "tiny_gea3_interface.h"
#include "tiny_timer.h"
#include "uptime_monitor.h"
}

#include "esphome_uart_adapter.h"
#include "esphome_mqtt_client_adapter.h"

// Forward declaration of the generated function
std::string appliance_type_to_string(uint8_t appliance_type);

namespace esphome {
namespace geappliances_bridge {

// Operation mode for the bridge
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

  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_device_id(const std::string &device_id) { this->configured_device_id_ = device_id; }
  void set_mode(uint8_t mode) { this->mode_ = static_cast<BridgeMode>(mode); }
  void set_polling_interval(uint32_t polling_interval) { this->polling_interval_ms_ = polling_interval; }

 protected:
  void on_mqtt_connected_();
  void notify_mqtt_disconnected_();
  void handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args);
  void initialize_mqtt_bridge_();
  void check_subscription_activity_();
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

  enum BridgeInitState {
    BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID,
    BRIDGE_INIT_STATE_WAITING_FOR_MQTT,
    BRIDGE_INIT_STATE_COMPLETE
  };

  uart::UARTComponent *uart_{nullptr};
  std::string configured_device_id_;
  std::string generated_device_id_;
  std::string final_device_id_;
  uint8_t client_address_{0xE4};
  bool mqtt_was_connected_{false};
  bool mqtt_bridge_initialized_{false};
  BridgeMode mode_{BRIDGE_MODE_AUTO};
  uint32_t polling_interval_ms_{10000};
  
  // Auto mode fallback tracking
  bool subscription_mode_active_{false};
  bool subscription_activity_detected_{false};
  uint32_t subscription_start_time_{0};
  static constexpr uint32_t SUBSCRIPTION_TIMEOUT_MS = 30000; // 30 seconds
  
  DeviceIdState device_id_state_{DEVICE_ID_STATE_IDLE};
  BridgeInitState bridge_init_state_{BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID};
  
  // Startup delay to allow WiFi to establish and capture early debug messages
  static constexpr uint32_t STARTUP_DELAY_MS = 20000; // 20 seconds
  uint32_t startup_time_{0};
  bool startup_delay_complete_{false};
  tiny_gea3_erd_client_request_id_t pending_request_id_;
  uint8_t appliance_type_{0};
  std::string model_number_;
  std::string serial_number_;
  uint32_t read_retry_count_{0};
  static constexpr uint32_t LOG_EVERY_N_RETRIES = 50; // Log retry attempts periodically
  static constexpr uint32_t MAX_READ_RETRIES = 1000; // Maximum retries before giving up (about 10 seconds at loop rate)

  tiny_timer_group_t timer_group_;

  esphome_uart_adapter_t uart_adapter_;
  esphome_mqtt_client_adapter_t mqtt_client_adapter_;

  tiny_gea3_interface_t gea3_interface_;
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[1000];

  tiny_gea3_erd_client_t erd_client_;
  uint8_t client_queue_buffer_[1024];

  mqtt_bridge_t mqtt_bridge_;
  mqtt_bridge_polling_t mqtt_bridge_polling_;

  uptime_monitor_t uptime_monitor_;
  
  tiny_event_subscription_t erd_client_activity_subscription_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
