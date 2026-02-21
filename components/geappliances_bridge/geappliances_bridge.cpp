#include "geappliances_bridge.h"
#include "esphome/core/log.h"

extern "C" {
#include "tiny_time_source.h"
}

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
  tiny_timer_group_init(&this->timer_group_, tiny_time_source_init());

  // Initialize UART adapter
  esphome_uart_adapter_init(&this->uart_adapter_, &this->timer_group_, this->uart_);

  // Initialize MQTT client adapter
  esphome_mqtt_client_adapter_init(&this->mqtt_client_adapter_, this->device_id_.c_str());

  // Initialize uptime monitor
  uptime_monitor_init(
    &this->uptime_monitor_,
    &this->timer_group_,
    &this->mqtt_client_adapter_.interface);

  // Initialize GEA3 interface
  tiny_gea3_interface_init(
    &this->gea3_interface_,
    &this->uart_adapter_.interface,
    this->client_address_,
    this->send_queue_buffer_,
    sizeof(this->send_queue_buffer_),
    this->receive_buffer_,
    sizeof(this->receive_buffer_),
    false);

  // Initialize ERD client
  tiny_gea3_erd_client_init(
    &this->erd_client_,
    &this->timer_group_,
    &this->gea3_interface_.interface,
    this->client_queue_buffer_,
    sizeof(this->client_queue_buffer_),
    &client_configuration);

  // Initialize MQTT bridge
  mqtt_bridge_init(
    &this->mqtt_bridge_,
    &this->timer_group_,
    &this->erd_client_.interface,
    &this->mqtt_client_adapter_.interface);

  ESP_LOGCONFIG(TAG, "GE Appliances Bridge setup complete");
}

void GeappliancesBridge::loop() {
  // Run timer group
  tiny_timer_group_run(&this->timer_group_);
  
  // Run GEA3 interface
  tiny_gea3_interface_run(&this->gea3_interface_);
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
