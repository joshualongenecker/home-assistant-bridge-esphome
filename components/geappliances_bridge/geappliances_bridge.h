#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/mqtt/mqtt_client.h"

extern "C" {
#include "mqtt_bridge.h"
#include "tiny_gea3_erd_client.h"
#include "tiny_gea3_interface.h"
#include "tiny_timer.h"
#include "uptime_monitor.h"
}

#include "esphome_uart_adapter.h"
#include "esphome_mqtt_client_adapter.h"

namespace esphome {
namespace geappliances_bridge {

class GeappliancesBridge : public Component {
 public:
  static constexpr unsigned long baud = 230400;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_device_id(const std::string &device_id) { this->device_id_ = device_id; }
  void set_client_address(uint8_t address) { this->client_address_ = address; }

 protected:
  void on_mqtt_connected_();
  void notify_mqtt_disconnected_();

  uart::UARTComponent *uart_{nullptr};
  std::string device_id_;
  uint8_t client_address_{0xE4};
  bool mqtt_was_connected_{false};

  tiny_timer_group_t timer_group_;

  esphome_uart_adapter_t uart_adapter_;
  esphome_mqtt_client_adapter_t mqtt_client_adapter_;

  tiny_gea3_interface_t gea3_interface_;
  uint8_t send_buffer_[255];
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[1000];

  tiny_gea3_erd_client_t erd_client_;
  uint8_t client_queue_buffer_[1024];

  mqtt_bridge_t mqtt_bridge_;

  uptime_monitor_t uptime_monitor_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
