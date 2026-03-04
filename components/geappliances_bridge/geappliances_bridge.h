#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include <string>

extern "C" {
#include "mqtt_bridge.h"
#include "mqtt_bridge_polling.h"
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

/*!
 * Structure for storing MQTT credentials in NVS (non-volatile storage).
 *
 * When ble_provisioning is enabled, the component stores MQTT credentials
 * here so they survive reboots and override compile-time secrets.
 *
 * Size limits (including null terminator):
 *   broker   – max 127 characters
 *   username – max 63 characters
 *   password – max 63 characters
 *
 * Note: only one geappliances_bridge instance per device is supported when
 * ble_provisioning is enabled (all instances share the same NVS slot).
 */
struct MQTTNvsCredentials {
  static constexpr uint8_t MAGIC = 0xBE;  ///< Detects valid stored data vs. blank flash
  char broker[128];
  uint16_t port;
  char username[64];
  char password[64];
  uint8_t magic;
};

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
  void set_device_id(const std::string &device_id) { this->configured_device_id_ = device_id; }
  void set_mode(uint8_t mode) { this->mode_ = static_cast<BridgeMode>(mode); }
  void set_polling_interval(uint32_t polling_interval) { this->polling_interval_ms_ = polling_interval; }
  void set_polling_only_publish_on_change(bool only_publish_on_change) { this->polling_only_publish_on_change_ = only_publish_on_change; }
  void set_ble_provisioning_enabled(bool enabled) { this->ble_provisioning_enabled_ = enabled; }

  /*!
   * Configure MQTT credentials at runtime and persist them to NVS.
   *
   * Call this via an ESPHome API service (see example.yaml) after BLE WiFi
   * provisioning to supply the MQTT broker credentials without hardcoding them
   * as compile-time secrets.  The credentials are written to flash and survive
   * reboots.  A safe reboot is triggered automatically so the MQTT component
   * re-connects with the new credentials on startup.
   *
   * @param broker   MQTT broker hostname or IP address (max 127 chars).
   * @param port     MQTT broker port (typically 1883).
   * @param username MQTT username – max 63 chars, may be empty.
   * @param password MQTT password – max 63 chars, may be empty.
   */
  void configure_mqtt_credentials(const std::string &broker, uint16_t port,
                                  const std::string &username, const std::string &password);

 protected:
  void on_mqtt_connected_();
  void notify_mqtt_disconnected_();
  void handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args);
  void initialize_mqtt_bridge_();
  void check_subscription_activity_();
  void run_autodiscovery_();
  void start_device_id_generation_();
  void process_device_id_erd_response_(tiny_erd_t erd, const uint8_t* data, uint8_t size);
  void handle_device_id_read_failure_(tiny_erd_t erd);
  std::string bytes_to_string_(const uint8_t* data, size_t size);
  std::string sanitize_for_mqtt_topic_(const std::string& input);
  bool try_read_erd_with_retry_(tiny_erd_t erd, const char* erd_name);
  bool load_mqtt_credentials_from_nvs_();
  void apply_nvs_mqtt_credentials_();

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
    AUTODISCOVERY_COMPLETE                   // At least one board discovered
  };

  uart::UARTComponent *uart_{nullptr};
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
  bool ble_provisioning_enabled_{false};
  MQTTNvsCredentials nvs_credentials_{};
  
  // Auto mode fallback tracking
  bool subscription_mode_active_{false};
  bool subscription_activity_detected_{false};
  uint32_t subscription_start_time_{0};
  static constexpr uint32_t SUBSCRIPTION_TIMEOUT_MS = 30000; // 30 seconds
  
  DeviceIdState device_id_state_{DEVICE_ID_STATE_IDLE};
  BridgeInitState bridge_init_state_{BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID};

  // Autodiscovery state machine
  AutodiscoveryState autodiscovery_state_{AUTODISCOVERY_WAITING_FOR_MQTT};
  uint32_t autodiscovery_timer_start_{0};
  bool gea3_board_discovered_{false};
  static constexpr uint32_t STARTUP_DELAY_MS = 20000;              // 20s after MQTT connects
  static constexpr uint32_t AUTODISCOVERY_BROADCAST_WINDOW_MS = 10000; // 10s window per broadcast

  tiny_gea3_erd_client_request_id_t pending_request_id_;
  uint8_t appliance_type_{0};
  std::string model_number_;
  std::string serial_number_;
  uint32_t read_retry_count_{0};
  static constexpr uint32_t LOG_EVERY_N_RETRIES = 50; // Log retry attempts periodically
  static constexpr uint32_t MAX_READ_RETRIES = 1000; // Maximum retries before giving up (about 10 seconds at loop rate)

  tiny_timer_group_t timer_group_;

  // GEA3 components
  esphome_uart_adapter_t uart_adapter_;
  esphome_mqtt_client_adapter_t mqtt_client_adapter_;

  tiny_gea3_interface_t gea3_interface_;
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[1000];

  tiny_gea3_erd_client_t erd_client_;
  uint8_t client_queue_buffer_[1024];

  i_tiny_gea3_erd_client_t* active_erd_client_{nullptr}; // set during initialize_mqtt_bridge_()

  mqtt_bridge_t mqtt_bridge_;
  mqtt_bridge_polling_t mqtt_bridge_polling_;

  tiny_event_subscription_t erd_client_activity_subscription_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
