#ifndef DEVICE_IDENTITY_MANAGER_H
#define DEVICE_IDENTITY_MANAGER_H

#include <string>
#include <cstdint>

extern "C" {
#include "tiny_gea3_erd_client.h"
}

namespace esphome {
namespace geappliances_bridge {

enum DeviceIdState {
  DEVICE_ID_STATE_READING_APPLIANCE_TYPE,
  DEVICE_ID_STATE_READING_MODEL_NUMBER,
  DEVICE_ID_STATE_READING_SERIAL_NUMBER,
  DEVICE_ID_STATE_COMPLETE,
};

class DeviceIdentityManager {
 public:
  void init(const char* configured_id,
            i_tiny_gea3_erd_client_t* erd_client,
            uint8_t host_address);

  void on_erd_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size);

  void on_erd_read_failed(tiny_erd_t erd);

  DeviceIdState get_state() const { return state_; }
  void cleanup();

  const char* get_device_id() const;

  const char* get_model_number() const { return model_number_; }
  uint8_t get_appliance_type() const { return appliance_type_; }

  const char* get_serial_number() const { return serial_number_; }

 private:
  bool try_queue_read_(tiny_erd_t erd);
  void bytes_to_string_(const uint8_t* data, size_t size, char* out, size_t out_size);
  std::string sanitize_for_mqtt_topic_(const char* input);

  DeviceIdState state_{DEVICE_ID_STATE_READING_APPLIANCE_TYPE};
  bool has_configured_device_id_{false};
  char configured_device_id_[92];
  char generated_device_id_[92];
  uint8_t appliance_type_{0};
  char model_number_[64];
  char serial_number_[64];
  tiny_gea3_erd_client_request_id_t pending_request_id_{0};

  i_tiny_gea3_erd_client_t* erd_client_{nullptr};
  uint8_t host_address_{0};
};

}
}

#endif
