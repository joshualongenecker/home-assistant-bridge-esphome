#include "geappliances_bridge.h"
#include "esphome/core/log.h"
#include "esphome_time_source.h"

namespace esphome {
namespace geappliances_bridge {

static const char *const TAG = "geappliances_bridge";

static const tiny_gea3_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};

void GeappliancesBridge::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

  // Initialize timer group
  ESP_LOGD(TAG, "Initializing timer group");
  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

  // Initialize UART adapter
  ESP_LOGD(TAG, "Initializing UART adapter with baud rate %lu", baud);
  esphome_uart_adapter_init(&this->uart_adapter_, &this->timer_group_, this->uart_);

  // Initialize MQTT client adapter
  ESP_LOGD(TAG, "Initializing MQTT client adapter for device: %s", this->device_id_.c_str());
  esphome_mqtt_client_adapter_init(&this->mqtt_client_adapter_, this->device_id_.c_str());

  // Initialize uptime monitor
  ESP_LOGD(TAG, "Initializing uptime monitor");
  uptime_monitor_init(
    &this->uptime_monitor_,
    &this->timer_group_,
    &this->mqtt_client_adapter_.interface);

  // Initialize GEA3 interface with older API that requires explicit send_buffer
  // Parameter order: self, uart, address, send_buffer, send_buffer_size,
  //                  receive_buffer, receive_buffer_size, send_queue_buffer,
  //                  send_queue_buffer_size, ignore_destination
  ESP_LOGD(TAG, "Initializing GEA3 interface with client address 0x%02X", this->client_address_);
  tiny_gea3_interface_init(
    &this->gea3_interface_,
    &this->uart_adapter_.interface,
    this->client_address_,
    this->send_buffer_,
    sizeof(this->send_buffer_),
    this->receive_buffer_,
    sizeof(this->receive_buffer_),
    this->send_queue_buffer_,
    sizeof(this->send_queue_buffer_),
    false);

  // Initialize ERD client
  ESP_LOGD(TAG, "Initializing ERD client");
  tiny_gea3_erd_client_init(
    &this->erd_client_,
    &this->timer_group_,
    &this->gea3_interface_.interface,
    this->client_queue_buffer_,
    sizeof(this->client_queue_buffer_),
    &client_configuration);

  // Initialize MQTT bridge
  ESP_LOGD(TAG, "Initializing MQTT bridge");
  mqtt_bridge_init(
    &this->mqtt_bridge_,
    &this->timer_group_,
    &this->erd_client_.interface,
    &this->mqtt_client_adapter_.interface);

  ESP_LOGCONFIG(TAG, "GE Appliances Bridge setup complete");
}

void GeappliancesBridge::loop() {
  // Check MQTT connection state
  auto mqtt_client = mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    bool is_connected = mqtt_client->is_connected();
    
    // Detect reconnection: was disconnected, now connected
    if (is_connected && !this->mqtt_was_connected_) {
      ESP_LOGI(TAG, "MQTT connection established");
      this->on_mqtt_connected_();
    }
    // Detect disconnection: was connected, now disconnected  
    else if (!is_connected && this->mqtt_was_connected_) {
      ESP_LOGW(TAG, "MQTT connection lost");
      // Note: We don't notify here because the bridge will handle it
      // when it tries to publish and fails
    }
    
    this->mqtt_was_connected_ = is_connected;
  } else {
    ESP_LOGVV(TAG, "MQTT client is null in loop()");
  }

  // Run timer group
  tiny_timer_group_run(&this->timer_group_);
  
  // Run GEA3 interface
  tiny_gea3_interface_run(&this->gea3_interface_);
}

void GeappliancesBridge::on_mqtt_connected_() {
  ESP_LOGI(TAG, "MQTT connected, notifying bridge to reset subscriptions");
  this->notify_mqtt_disconnected_();
}

void GeappliancesBridge::notify_mqtt_disconnected_() {
  // Notify the MQTT adapter that we disconnected
  // This will clear the ERD registry and trigger resubscription
  esphome_mqtt_client_adapter_notify_disconnected(&this->mqtt_client_adapter_);
}

void GeappliancesBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "GE Appliances Bridge:");
  ESP_LOGCONFIG(TAG, "  Device ID: %s", this->device_id_.c_str());
  ESP_LOGCONFIG(TAG, "  Client Address: 0x%02X", this->client_address_);
  ESP_LOGCONFIG(TAG, "  UART Baud Rate: %lu", baud);
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after UART (priority 600) and MQTT (priority 50)
  return setup_priority::DATA;  // Priority 600
}

}  // namespace geappliances_bridge
}  // namespace esphome
