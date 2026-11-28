#pragma once

#include <cstdint>

#include "bridge_mode.h"
#include "erd_bridge_common.h"

namespace esphome {
namespace geappliances_bridge {

class IBridgeServices {
 public:
  virtual ~IBridgeServices() = default;

  virtual void run_autodiscovery() = 0;
  virtual bool is_autodiscovery_complete() const = 0;
  virtual uint8_t get_discovered_host_address() const = 0;
  virtual bool is_discovered_gea2_protocol() const = 0;

  virtual void init_device_id_reading() = 0;
  virtual bool is_device_id_complete() const = 0;

  virtual bool is_mqtt_client_initialized() const = 0;
  virtual void initialize_mqtt_client() = 0;

  virtual void start_feature_bit_reading() = 0;
  virtual bool is_feature_bits_complete() const = 0;

  virtual bool is_bridge_initialized() const = 0;
  virtual void initialize_erd_bridge() = 0;

  virtual BridgeMode get_mode() const = 0;
  virtual subscription_state_t get_subscription_state() const = 0;
  virtual polling_state_t get_polling_state() const = 0;

  virtual void record_startup_delay_start() = 0;
  virtual bool is_startup_delay_elapsed() const = 0;

  virtual void maybe_start_custom_erd_polling() = 0;
  virtual void handle_subscription_failed() = 0;
  virtual void handle_polling_failed() = 0;
  virtual void log_poll_state_transitions() = 0;
  virtual bool check_steady_state() = 0;

  virtual void initialize_erd_cache_publisher() = 0;
  virtual bool is_erd_cache_publisher_initialized() const = 0;
};

}
}
