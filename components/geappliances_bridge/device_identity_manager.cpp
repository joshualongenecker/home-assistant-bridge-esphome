
#include "device_identity_manager.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include <cstring>
#include <inttypes.h>

#include "appliance_type_map.h"

namespace esphome {
namespace geappliances_bridge {

GEA_TAG(TAG) = "device_identity";

void DeviceIdentityManager::init(const char* configured_id,
                                  i_tiny_gea3_erd_client_t* erd_client,
                                  uint8_t host_address)
{
  this->erd_client_ = erd_client;
  this->host_address_ = host_address;
  this->state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
  this->generated_device_id_[0] = '\0';
  this->model_number_[0] = '\0';
  this->serial_number_[0] = '\0';

  if (configured_id != nullptr && configured_id[0] != '\0') {
    strncpy(this->configured_device_id_, configured_id, sizeof(this->configured_device_id_) - 1);
    this->configured_device_id_[sizeof(this->configured_device_id_) - 1] = '\0';
    this->has_configured_device_id_ = true;
    ESP_LOGI(TAG, "Device ID configured: %s (will still read identity ERDs)",
             this->configured_device_id_);
  } else {
    this->has_configured_device_id_ = false;
    this->configured_device_id_[0] = '\0';
    ESP_LOGI(TAG, "No device_id configured, will auto-generate from identity ERDs");
  }

  this->try_queue_read_(ERD_APPLIANCE_TYPE);
}
void DeviceIdentityManager::cleanup()
{
  this->state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
  this->has_configured_device_id_ = false;
  this->configured_device_id_[0] = '\0';
  this->generated_device_id_[0] = '\0';
  this->appliance_type_ = 0;
  this->model_number_[0] = '\0';
  this->serial_number_[0] = '\0';
  this->pending_request_id_ = 0;
  this->erd_client_ = nullptr;
  this->host_address_ = 0;
}

void DeviceIdentityManager::on_erd_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size)
{
  if (erd == ERD_APPLIANCE_TYPE) {
    if (size < 1) return;
    this->appliance_type_ = data[0];
    ESP_LOGI(TAG, "Read appliance type: %u", this->appliance_type_);
    this->state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
    this->try_queue_read_(ERD_MODEL_NUMBER);

  } else if (erd == ERD_MODEL_NUMBER) {
    this->bytes_to_string_(data, size, this->model_number_, sizeof(this->model_number_));
    ESP_LOGI(TAG, "Read model number: %s", this->model_number_);
    this->state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
    this->try_queue_read_(ERD_SERIAL_NUMBER);

  } else if (erd == ERD_SERIAL_NUMBER) {
    this->bytes_to_string_(data, size, this->serial_number_, sizeof(this->serial_number_));
    ESP_LOGI(TAG, "Read serial number: %s", this->serial_number_);

    /* Build generated_device_id: applianceType_model_serial */
    const char* type_str = appliance_type_to_string(this->appliance_type_);
    const std::string sanitized_model = this->sanitize_for_mqtt_topic_(this->model_number_);
    const std::string sanitized_serial = this->sanitize_for_mqtt_topic_(this->serial_number_);
    snprintf(this->generated_device_id_, sizeof(this->generated_device_id_),
             "%s_%s_%s", type_str, sanitized_model.c_str(), sanitized_serial.c_str());
    ESP_LOGI(TAG, "Generated device ID: %s", this->generated_device_id_);
    this->state_ = DEVICE_ID_STATE_COMPLETE;

  } else {
    ESP_LOGW(TAG, "Unexpected ERD 0x%04X in on_erd_read_completed", erd);
  }
}

void DeviceIdentityManager::on_erd_read_failed(tiny_erd_t erd)
{
  // Retries indefinitely -- never give up.
  ESP_LOGW(TAG, "Failed to read ERD 0x%04X for device ID generation, retrying", erd);
  if (!this->try_queue_read_(erd)) {
    ESP_LOGD(TAG, "Could not re-queue ERD 0x%04X read (client unavailable)", erd);
  }
}

const char* DeviceIdentityManager::get_device_id() const
{
  if (this->has_configured_device_id_) {
    return this->configured_device_id_;
  }
  return this->generated_device_id_;
}

bool DeviceIdentityManager::try_queue_read_(tiny_erd_t erd)
{
  if (this->erd_client_ == nullptr) {
    return false;
  }

  return tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                    this->host_address_, erd);
}

void DeviceIdentityManager::bytes_to_string_(const uint8_t* data, size_t size,
                                              char* out, size_t out_size)
{
  if (size == 0) {
    out[0] = '\0';
    return;
  }
  // GE API model/serial are plain ASCII, null-terminated or padded with 0x00.
  // Some appliances pad trailing bytes with '_' (underscore); strip it.
  size_t i = 0;
  for (; i < size && i < out_size - 1; i++) {
    uint8_t raw = data[i];
    if (raw == 0x00) break;
    out[i] = static_cast<char>(raw);
  }
  out[i] = '\0';
  while (i > 0 && out[i - 1] == '_') {
    out[i - 1] = '\0';
    i--;
  }
}

std::string DeviceIdentityManager::sanitize_for_mqtt_topic_(const char* input)
{
  std::string result;
  result.reserve(strlen(input));
  for (const char* c = input; *c; c++) {
    if (*c == '+' || *c == '#' || *c == '/' || *c == '$' || *c == ' ' ||
        static_cast<unsigned char>(*c) < 0x20 || static_cast<unsigned char>(*c) > 0x7E) {
      result += '_';
    } else {
      result += *c;
    }
  }
  return result.empty() ? "Unknown" : result;
}

}  // namespace geappliances_bridge
}  // namespace esphome
