/*!
 * @file
 * @brief DeviceIdentityManager implementation.
 *
 * Adapted from geappliances_bridge_device_id.cpp as part of the god class
 * refactoring. Logic is identical - only the class context changes.
 */

#include "device_identity_manager.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"

// Forward declaration (generated from appliance API data)
std::string appliance_type_to_string(uint8_t appliance_type);

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "device_identity";

void DeviceIdentityManager::init(const std::string& configured_id,
                                  i_tiny_gea3_erd_client_t* erd_client,
                                  uint8_t host_address)
{
  this->configured_device_id_ = configured_id;
  this->erd_client_ = erd_client;
  this->host_address_ = host_address;
  this->queue_retry_count_ = 0;
  this->response_retries_ = 0;

  if (!this->configured_device_id_.empty()) {
    ESP_LOGI(TAG, "Using configured device_id: %s", this->configured_device_id_.c_str());
    this->final_device_id_ = this->configured_device_id_;
    this->state_ = DEVICE_ID_STATE_COMPLETE;
  } else {
    this->state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
  }
}

void DeviceIdentityManager::run()
{
  // If a device_id was pre-configured, we're already complete.
  if (this->state_ == DEVICE_ID_STATE_COMPLETE) {
    return;
  }

  if (this->erd_client_ == nullptr) {
    return;
  }

  if (this->state_ == DEVICE_ID_STATE_READING_APPLIANCE_TYPE) {
    this->try_read_erd_with_retry_(ERD_APPLIANCE_TYPE, "appliance type");
  } else if (this->state_ == DEVICE_ID_STATE_READING_MODEL_NUMBER) {
    this->try_read_erd_with_retry_(ERD_MODEL_NUMBER, "model number");
  } else if (this->state_ == DEVICE_ID_STATE_READING_SERIAL_NUMBER) {
    this->try_read_erd_with_retry_(ERD_SERIAL_NUMBER, "serial number");
  }
}

void DeviceIdentityManager::on_erd_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size)
{
  this->response_retries_ = 0;

  if (erd == ERD_APPLIANCE_TYPE) {
    if (size < 1) return;
    this->appliance_type_ = data[0];
    ESP_LOGI(TAG, "Read appliance type: %u", this->appliance_type_);
    if (this->erd_client_ != nullptr &&
        tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                   this->host_address_, ERD_MODEL_NUMBER)) {
      this->state_ = DEVICE_ID_STATE_IDLE;
    } else {
      this->state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
    }
  } else if (erd == ERD_MODEL_NUMBER) {
    this->model_number_ = this->bytes_to_string_(data, size);
    ESP_LOGI(TAG, "Read model number: %s", this->model_number_.c_str());
    if (this->erd_client_ != nullptr &&
        tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                   this->host_address_, ERD_SERIAL_NUMBER)) {
      this->state_ = DEVICE_ID_STATE_IDLE;
    } else {
      this->state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
    }
  } else if (erd == ERD_SERIAL_NUMBER) {
    this->serial_number_ = this->bytes_to_string_(data, size);
    ESP_LOGI(TAG, "Read serial number: %s", this->serial_number_.c_str());
    this->generated_device_id_ = appliance_type_to_string(this->appliance_type_) + "_" +
                                  this->sanitize_for_mqtt_topic_(this->model_number_) + "_" +
                                  this->sanitize_for_mqtt_topic_(this->serial_number_);
    this->final_device_id_ = this->generated_device_id_;
    ESP_LOGI(TAG, "Generated device ID: %s", this->final_device_id_.c_str());
    this->state_ = DEVICE_ID_STATE_COMPLETE;
  }
}

void DeviceIdentityManager::on_erd_read_failed(tiny_erd_t erd)
{
  this->response_retries_++;
  ESP_LOGW(TAG, "Failed to read ERD 0x%04X for device ID generation (reason: %u), retry %u",
           erd, this->response_retries_);

  if (this->response_retries_ >= MAX_DEVICE_ID_RESPONSE_RETRIES) {
    ESP_LOGW(TAG, "Max retries reached for ERD 0x%04X, using fallback value", erd);
    this->response_retries_ = 0;

    if (erd == ERD_APPLIANCE_TYPE) {
      this->appliance_type_ = 0xFF;  // Unknown
      if (this->erd_client_ != nullptr &&
          tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                     this->host_address_, ERD_MODEL_NUMBER)) {
        this->state_ = DEVICE_ID_STATE_IDLE;
      } else {
        this->state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
      }
    } else if (erd == ERD_MODEL_NUMBER) {
      this->model_number_ = "Unknown";
      if (this->erd_client_ != nullptr &&
          tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                     this->host_address_, ERD_SERIAL_NUMBER)) {
        this->state_ = DEVICE_ID_STATE_IDLE;
      } else {
        this->state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
      }
    } else if (erd == ERD_SERIAL_NUMBER) {
      this->serial_number_ = "Unknown";
      this->generated_device_id_ = appliance_type_to_string(this->appliance_type_) + "_" +
                                    this->sanitize_for_mqtt_topic_(this->model_number_) + "_" +
                                    this->sanitize_for_mqtt_topic_(this->serial_number_);
      this->final_device_id_ = this->generated_device_id_;
      ESP_LOGW(TAG, "Generated device ID with fallback: %s", this->final_device_id_.c_str());
      this->state_ = DEVICE_ID_STATE_COMPLETE;
    }
  } else {
    // Reset to the reading state so run() will retry
    if (erd == ERD_APPLIANCE_TYPE) {
      this->state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
    } else if (erd == ERD_MODEL_NUMBER) {
      this->state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
    } else if (erd == ERD_SERIAL_NUMBER) {
      this->state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
    }
  }
}

bool DeviceIdentityManager::is_complete() const
{
  return this->state_ == DEVICE_ID_STATE_COMPLETE;
}

bool DeviceIdentityManager::is_failed() const
{
  return this->state_ == DEVICE_ID_STATE_FAILED;
}

const std::string& DeviceIdentityManager::get_device_id() const
{
  return this->final_device_id_;
}

uint8_t DeviceIdentityManager::get_appliance_type() const
{
  return this->appliance_type_;
}

const std::string& DeviceIdentityManager::get_generated_device_id() const
{
  return this->generated_device_id_;
}

const std::string& DeviceIdentityManager::get_model_number() const
{
  return this->model_number_;
}

const std::string& DeviceIdentityManager::get_serial_number() const
{
  return this->serial_number_;
}

bool DeviceIdentityManager::try_read_erd_with_retry_(tiny_erd_t erd, const char* erd_name)
{
  /* Suppress unused parameter warning when ESP_LOG macros are no-ops. */
  (void)erd_name;

  if (this->erd_client_ == nullptr) {
    return false;
  }

  if (tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                 this->host_address_, erd)) {
    this->queue_retry_count_ = 0;
    this->state_ = DEVICE_ID_STATE_IDLE;
    return true;
  }

  this->queue_retry_count_++;
  if (this->queue_retry_count_ % LOG_EVERY_N_RETRIES == 0) {
    ESP_LOGW(TAG, "Queue full, unable to send %s read (retry %u)",
             erd_name, this->queue_retry_count_);
  }
  if (this->queue_retry_count_ >= MAX_QUEUE_RETRIES) {
    ESP_LOGE(TAG, "Max queue retries reached for %s, giving up", erd_name);
    this->state_ = DEVICE_ID_STATE_FAILED;
    return false;
  }
  return false;
}

std::string DeviceIdentityManager::bytes_to_string_(const uint8_t* data, size_t size)
{
  if (size == 0) return "";
  std::string result(reinterpret_cast<const char*>(data), size);
  // Trim trailing null bytes
  while (!result.empty() && result.back() == '\0') {
    result.pop_back();
  }
  return result;
}

std::string DeviceIdentityManager::sanitize_for_mqtt_topic_(const std::string& input)
{
  std::string result;
  result.reserve(input.size());
  for (char c : input) {
    if (c == '+' || c == '#' || c == '/' || c == '$' || c == ' ' ||
        static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
      result += '_';
    } else {
      result += c;
    }
  }
  return result.empty() ? "Unknown" : result;
}

}  // namespace geappliances_bridge
}  // namespace esphome
