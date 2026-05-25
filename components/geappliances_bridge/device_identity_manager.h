/*!
 * @file
 * @brief Device identity manager for the GEA bridge.
 *
 * Handles reading appliance type, model number, and serial number ERDs,
 * then assembles a unique MQTT-topic-safe device identifier string.
 */

#ifndef DEVICE_IDENTITY_MANAGER_H
#define DEVICE_IDENTITY_MANAGER_H

#include <string>
#include <cstdint>

extern "C" {
#include "tiny_gea3_erd_client.h"
}

namespace esphome {
namespace geappliances_bridge {

// Device ID generation states (must match existing enum in geappliances_bridge.h)
enum DeviceIdState {
  DEVICE_ID_STATE_IDLE,
  DEVICE_ID_STATE_READING_APPLIANCE_TYPE,
  DEVICE_ID_STATE_READING_MODEL_NUMBER,
  DEVICE_ID_STATE_READING_SERIAL_NUMBER,
  DEVICE_ID_STATE_COMPLETE,
  DEVICE_ID_STATE_FAILED,
};

// Forward declaration
class GeappliancesBridge;

/*!
 * Manages device ID generation from appliance identity ERDs.
 *
 * Reads ERDs 0x0008 (appliance type), 0x0001 (model number), and 0x0002
 * (serial number), then assembles a unique device identifier.
 * If a device_id is pre-configured in YAML, the read sequence is skipped.
 */
class DeviceIdentityManager {
 public:
  static constexpr uint32_t MAX_QUEUE_RETRIES = 1000;
  static constexpr uint32_t MAX_DEVICE_ID_RESPONSE_RETRIES = 3;
  static constexpr uint32_t LOG_EVERY_N_RETRIES = 50;

  /*!
   * Initialize with configured device ID (if any), ERD client, and host address.
   */
  void init(const std::string& configured_id,
            i_tiny_gea3_erd_client_t* erd_client,
            uint8_t host_address);

  /*!
   * Called from loop(). Attempts to queue the next ERD read if in a READING_* state.
   */
  void run();

  /*!
   * Called when an ERD read completes successfully during device ID generation.
   */
  void on_erd_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size);

  /*!
   * Called when an ERD read fails during device ID generation.
   */
  void on_erd_read_failed(tiny_erd_t erd);

  /*!
   * Check if device ID generation is complete (success or configured ID).
   */
  bool is_complete() const;

  /*!
   * Check if device ID generation has failed.
   */
  bool is_failed() const;

  /*!
   * Get the final device ID string.
   */
  const std::string& get_device_id() const;

  /*!
   * Get the appliance type byte.
   */
  uint8_t get_appliance_type() const;

  /*!
   * Get the generated device ID (before fallback).
   */
  const std::string& get_generated_device_id() const;

  /*!
   * Get the model number string.
   */
  const std::string& get_model_number() const;

  /*!
   * Get the serial number string.
   */
  const std::string& get_serial_number() const;

  /*!
   * Get the current state.
   */
  DeviceIdState get_state() const { return state_; }

  /*!
   * Get the current queue retry count. (Test-only.)
   */
  uint32_t get_queue_retry_count() const { return queue_retry_count_; }

 private:
  bool try_read_erd_with_retry_(tiny_erd_t erd, const char* erd_name);
  std::string bytes_to_string_(const uint8_t* data, size_t size);
  std::string sanitize_for_mqtt_topic_(const std::string& input);

  DeviceIdState state_{DEVICE_ID_STATE_IDLE};
  std::string configured_device_id_;
  std::string generated_device_id_;
  std::string final_device_id_;
  uint8_t appliance_type_{0};
  std::string model_number_;
  std::string serial_number_;
  uint32_t queue_retry_count_{0};
  uint32_t response_retries_{0};
  tiny_gea3_erd_client_request_id_t pending_request_id_{0};

  i_tiny_gea3_erd_client_t* erd_client_{nullptr};
  uint8_t host_address_{0};
};

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // DEVICE_IDENTITY_MANAGER_H
