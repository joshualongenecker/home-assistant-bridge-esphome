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

// ERD identifiers for device ID generation
static constexpr tiny_erd_t ERD_MODEL_NUMBER = 0x0001;
static constexpr tiny_erd_t ERD_SERIAL_NUMBER = 0x0002;
static constexpr tiny_erd_t ERD_APPLIANCE_TYPE = 0x0008;
// GEA3 appliance host address (GEA2 uses 0xA0, defined in gea2_mqtt_bridge.cpp)
static constexpr uint8_t ERD_HOST_ADDRESS = 0xC0;

void GeappliancesBridge::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

  // Record startup time for delay
  this->startup_time_ = millis();
  ESP_LOGI(TAG, "Startup delay: waiting %u seconds before initializing", STARTUP_DELAY_MS / 1000);

  // Initialize timer group
  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

  // Initialize UART adapter
  esphome_uart_adapter_init(&this->uart_adapter_, &this->timer_group_, this->uart_);

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

  // Subscribe to ERD client activity
  tiny_event_subscription_init(
    &this->erd_client_activity_subscription_, 
    this, 
    +[](void* context, const void* args) {
      auto self = reinterpret_cast<GeappliancesBridge*>(context);
      auto activity_args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
      self->handle_erd_client_activity_(activity_args);
    });
  tiny_event_subscribe(
    tiny_gea3_erd_client_on_activity(&this->erd_client_.interface), 
    &this->erd_client_activity_subscription_);

  // Determine if we should auto-generate device ID or use configured one
  if (this->configured_device_id_.empty()) {
    ESP_LOGI(TAG, "No device_id configured, will auto-generate from appliance ERDs");
    this->device_id_state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
  } else {
    ESP_LOGI(TAG, "Using configured device_id: %s", this->configured_device_id_.c_str());
    this->final_device_id_ = this->configured_device_id_;
    this->device_id_state_ = DEVICE_ID_STATE_COMPLETE;
    // Don't initialize MQTT bridge yet - wait for MQTT connection
    this->bridge_init_state_ = BRIDGE_INIT_STATE_WAITING_FOR_MQTT;
  }

  // Initialize GEA2 if configured
  if (this->gea2_uart_ != nullptr) {
    this->gea2_enabled_ = true;
    ESP_LOGI(TAG, "GEA2 enabled, initializing GEA2 components...");
    
    // Initialize GEA2 UART adapter
    esphome_uart_adapter_init(&this->gea2_uart_adapter_, &this->timer_group_, this->gea2_uart_);
    
    // Initialize GEA2 interface
    static const tiny_gea2_erd_client_configuration_t gea2_client_configuration = {
      .request_timeout = 250,
      .request_retries = 10
    };
    
    tiny_gea2_interface_init(
      &this->gea2_interface_,
      &this->gea2_uart_adapter_.interface,
      esphome_time_source_init(),
      nullptr,  // msec_interrupt (not used in ESPHome)
      this->gea2_client_address_,
      this->gea2_send_queue_buffer_,
      sizeof(this->gea2_send_queue_buffer_),
      this->gea2_receive_buffer_,
      sizeof(this->gea2_receive_buffer_),
      false,
      1);
    
    // Initialize GEA2 ERD client
    tiny_gea2_erd_client_init(
      &this->gea2_erd_client_,
      &this->timer_group_,
      &this->gea2_interface_.interface,
      this->gea2_client_queue_buffer_,
      sizeof(this->gea2_client_queue_buffer_),
      &gea2_client_configuration);
    
    ESP_LOGI(TAG, "GEA2 components initialized, waiting for MQTT to initialize bridge");
  } else {
    ESP_LOGI(TAG, "GEA2 disabled (no gea2_uart_id configured)");
  }

  ESP_LOGCONFIG(TAG, "GE Appliances Bridge setup complete");
}

void GeappliancesBridge::loop() {
  // Enforce startup delay to allow WiFi to establish and capture early debug messages
  if (!this->startup_delay_complete_) {
    // Note: Unsigned subtraction wraps correctly even when millis() overflows after ~49 days
    if (millis() - this->startup_time_ >= STARTUP_DELAY_MS) {
      this->startup_delay_complete_ = true;
      ESP_LOGI(TAG, "Startup delay complete, beginning normal operation");
    } else {
      // Skip all processing during startup delay
      return;
    }
  }

  // Check MQTT connection state
  auto mqtt_client = mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    bool is_connected = mqtt_client->is_connected();
    
    // Detect reconnection: was disconnected, now connected
    if (is_connected && !this->mqtt_was_connected_) {
      this->on_mqtt_connected_();
    }
    // Detect disconnection: was connected, now disconnected  
    else if (!is_connected && this->mqtt_was_connected_) {
      // Note: We don't notify here because the bridge will handle it
      // when it tries to publish and fails
    }
    
    this->mqtt_was_connected_ = is_connected;
  }

  // Run timer group
  tiny_timer_group_run(&this->timer_group_);
  
  // Run GEA3 interface
  tiny_gea3_interface_run(&this->gea3_interface_);

  // Run GEA2 interface if enabled
  if (this->gea2_enabled_) {
    tiny_gea2_interface_run(&this->gea2_interface_);
  }

  // Initialize MQTT bridge when device ID is ready and MQTT is connected
  if (this->bridge_init_state_ == BRIDGE_INIT_STATE_WAITING_FOR_MQTT && 
      mqtt_client != nullptr && mqtt_client->is_connected()) {
    ESP_LOGI(TAG, "Device ID ready and MQTT connected, initializing MQTT bridge");
    this->initialize_mqtt_bridge_();
    this->bridge_init_state_ = BRIDGE_INIT_STATE_COMPLETE;
  }

  // Initialize GEA2 MQTT bridge when MQTT is connected (only once)
  if (this->gea2_enabled_ && !this->gea2_bridge_initialized_ && 
      mqtt_client != nullptr && mqtt_client->is_connected()) {
    this->initialize_gea2_bridge_();
  }

  // Check for subscription activity timeout in auto mode
  if (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_) {
    this->check_subscription_activity_();
  }

  // Handle device ID generation state machine
  // Note: If state reaches DEVICE_ID_STATE_FAILED, device requires reboot to retry
  if (this->device_id_state_ == DEVICE_ID_STATE_READING_APPLIANCE_TYPE) {
    this->try_read_erd_with_retry_(ERD_APPLIANCE_TYPE, "appliance type");
  } else if (this->device_id_state_ == DEVICE_ID_STATE_READING_MODEL_NUMBER) {
    this->try_read_erd_with_retry_(ERD_MODEL_NUMBER, "model number");
  } else if (this->device_id_state_ == DEVICE_ID_STATE_READING_SERIAL_NUMBER) {
    this->try_read_erd_with_retry_(ERD_SERIAL_NUMBER, "serial number");
  }
}

void GeappliancesBridge::on_mqtt_connected_() {
  ESP_LOGI(TAG, "MQTT connected, flushing pending updates and resetting subscriptions");
  
  // Flush any pending ERD updates that were queued while MQTT was not connected
  if (this->mqtt_bridge_initialized_) {
    esphome_mqtt_client_adapter_notify_connected(&this->mqtt_client_adapter_);
  }
  
  // Notify bridge to reset subscriptions
  // Note: This notifies the bridge as if MQTT disconnected, which triggers the bridge
  // to clear its ERD registry and resubscribe. This ensures all ERDs are re-registered
  // and subscriptions are fresh after reconnection.
  this->notify_mqtt_disconnected_();
}

void GeappliancesBridge::notify_mqtt_disconnected_() {
  // Only notify if MQTT bridge is initialized
  if (this->mqtt_bridge_initialized_) {
    // Notify the MQTT adapter that we disconnected
    // This will clear the ERD registry and trigger resubscription
    esphome_mqtt_client_adapter_notify_disconnected(&this->mqtt_client_adapter_);
  }
}

void GeappliancesBridge::handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args) {
  // Track subscription activity for auto mode
  if (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_) {
    // Check if this is a subscription publication (ERD data received from subscription)
    if (this->mqtt_bridge_initialized_ && 
        args->address == ERD_HOST_ADDRESS &&
        args->type == tiny_gea3_erd_client_activity_type_subscription_publication_received) {
      if (!this->subscription_activity_detected_) {
        ESP_LOGI(TAG, "Subscription activity detected - subscription mode is working");
        this->subscription_activity_detected_ = true;
      }
    }
  }

  // Only process read responses for device ID ERDs before MQTT bridge is initialized
  if (!this->mqtt_bridge_initialized_ && args->address == ERD_HOST_ADDRESS) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed) {
      if (args->read_completed.erd == ERD_APPLIANCE_TYPE) {
        // Appliance type is a single byte enum
        this->appliance_type_ = reinterpret_cast<const uint8_t*>(args->read_completed.data)[0];
        ESP_LOGI(TAG, "Read appliance type: %u", this->appliance_type_);
        this->device_id_state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
      } else if (args->read_completed.erd == ERD_MODEL_NUMBER) {
        // Model number is a 32-byte string
        this->model_number_ = this->bytes_to_string_(
          reinterpret_cast<const uint8_t*>(args->read_completed.data), 
          args->read_completed.data_size);
        ESP_LOGI(TAG, "Read model number: %s", this->model_number_.c_str());
        this->device_id_state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
      } else if (args->read_completed.erd == ERD_SERIAL_NUMBER) {
        // Serial number is a 32-byte string
        this->serial_number_ = this->bytes_to_string_(
          reinterpret_cast<const uint8_t*>(args->read_completed.data), 
          args->read_completed.data_size);
        ESP_LOGI(TAG, "Read serial number: %s", this->serial_number_.c_str());
        
        // Sanitize strings for MQTT topic use
        std::string sanitized_model = this->sanitize_for_mqtt_topic_(this->model_number_);
        std::string sanitized_serial = this->sanitize_for_mqtt_topic_(this->serial_number_);
        
        // Convert appliance type to string name using generated function
        std::string appliance_type_name = appliance_type_to_string(this->appliance_type_);
        
        // Generate device ID with appliance type name
        this->generated_device_id_ = appliance_type_name + "_" + 
                                     sanitized_model + "_" + 
                                     sanitized_serial;
        this->final_device_id_ = this->generated_device_id_;
        
        ESP_LOGI(TAG, "Generated device ID: %s", this->final_device_id_.c_str());
        
        this->device_id_state_ = DEVICE_ID_STATE_COMPLETE;
        // Don't initialize MQTT bridge yet - wait for MQTT connection
        this->bridge_init_state_ = BRIDGE_INIT_STATE_WAITING_FOR_MQTT;
      }
    } else if (args->type == tiny_gea3_erd_client_activity_type_read_failed) {
      // Log the failure and retry by transitioning back to the appropriate reading state
      ESP_LOGW(TAG, "Failed to read ERD 0x%04X for device ID generation (reason: %u), will retry", 
               args->read_failed.erd, args->read_failed.reason);
      
      // Transition back to the reading state to retry
      if (args->read_failed.erd == ERD_APPLIANCE_TYPE) {
        this->device_id_state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
      } else if (args->read_failed.erd == ERD_MODEL_NUMBER) {
        this->device_id_state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
      } else if (args->read_failed.erd == ERD_SERIAL_NUMBER) {
        this->device_id_state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
      }
    }
  }
}

void GeappliancesBridge::initialize_mqtt_bridge_() {
  if (this->mqtt_bridge_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "Initializing MQTT bridge with device ID: %s", this->final_device_id_.c_str());
  
  // Determine the actual mode to use for initialization
  bool use_polling = false;
  const char* mode_name = "unknown";
  
  if (this->mode_ == BRIDGE_MODE_POLL) {
    use_polling = true;
    mode_name = "polling";
  } else if (this->mode_ == BRIDGE_MODE_SUBSCRIBE) {
    use_polling = false;
    mode_name = "subscription";
  } else if (this->mode_ == BRIDGE_MODE_AUTO) {
    use_polling = false;
    mode_name = "auto (starting with subscription)";
    this->subscription_mode_active_ = true;
    this->subscription_activity_detected_ = false;
    this->subscription_start_time_ = millis();
  }
  
  ESP_LOGI(TAG, "Using %s mode with polling interval: %u ms", mode_name, this->polling_interval_ms_);

  // Initialize MQTT client adapter
  esphome_mqtt_client_adapter_init(&this->mqtt_client_adapter_, this->final_device_id_.c_str());

  // Initialize uptime monitor
  uptime_monitor_init(
    &this->uptime_monitor_,
    &this->timer_group_,
    &this->mqtt_client_adapter_.interface);

  // Initialize MQTT bridge based on mode
  if (use_polling) {
    mqtt_bridge_polling_init(
      &this->mqtt_bridge_polling_,
      &this->timer_group_,
      &this->erd_client_.interface,
      &this->mqtt_client_adapter_.interface,
      this->polling_interval_ms_);
  } else {
    mqtt_bridge_init(
      &this->mqtt_bridge_,
      &this->timer_group_,
      &this->erd_client_.interface,
      &this->mqtt_client_adapter_.interface);
  }

  this->mqtt_bridge_initialized_ = true;
  ESP_LOGI(TAG, "MQTT bridge initialized successfully");
}

std::string GeappliancesBridge::bytes_to_string_(const uint8_t* data, size_t size) {
  // Validate input
  if (data == nullptr || size == 0) {
    return "";
  }
  
  // Convert byte data to string, stopping at first null byte
  std::string result;
  result.reserve(size);
  for (size_t i = 0; i < size; i++) {
    if (data[i] == 0x00) {
      break; // Stop at null terminator
    }
    result += static_cast<char>(data[i]);
  }
  return result;
}

std::string GeappliancesBridge::sanitize_for_mqtt_topic_(const std::string& input) {
  // MQTT topic names should not contain: +, #, null character, and ideally avoid spaces
  // Replace invalid characters with underscores
  std::string result;
  result.reserve(input.length());
  
  for (char c : input) {
    if (c == '+' || c == '#' || c == '\0' || c == ' ' || c == '/' || c == '$') {
      result += '_';
    } else if (c < 32 || c > 126) {
      // Replace non-printable and extended ASCII characters
      result += '_';
    } else {
      result += c;
    }
  }
  
  return result;
}

bool GeappliancesBridge::try_read_erd_with_retry_(tiny_erd_t erd, const char* erd_name) {
  if (tiny_gea3_erd_client_read(&this->erd_client_.interface, &this->pending_request_id_, 
                                 ERD_HOST_ADDRESS, erd)) {
    ESP_LOGD(TAG, "Reading %s ERD 0x%04X", erd_name, erd);
    this->device_id_state_ = DEVICE_ID_STATE_IDLE; // Wait for response
    this->read_retry_count_ = 0;
    return true;
  } else {
    // Failed to queue the read request, will retry on next loop
    this->read_retry_count_++;
    if (this->read_retry_count_ >= MAX_READ_RETRIES) {
      ESP_LOGE(TAG, "Failed to read %s after %u retries, giving up", erd_name, MAX_READ_RETRIES);
      this->device_id_state_ = DEVICE_ID_STATE_FAILED;
      return false;
    } else if (this->read_retry_count_ % LOG_EVERY_N_RETRIES == 0) {
      ESP_LOGW(TAG, "Failed to queue %s read, retrying... (attempt %u)", erd_name, this->read_retry_count_);
    }
    return false;
  }
}

void GeappliancesBridge::check_subscription_activity_() {
  // If we already detected activity, no need to check
  if (this->subscription_activity_detected_) {
    return;
  }
  
  // Check if timeout has elapsed
  // Note: Unsigned subtraction wraps correctly even when millis() overflows (every ~49.7 days).
  // This assumes the timeout check occurs at least once per overflow period, which is guaranteed
  // since the timeout is 30 seconds and loop() runs continuously.
  uint32_t elapsed = millis() - this->subscription_start_time_;
  if (elapsed >= SUBSCRIPTION_TIMEOUT_MS) {
    ESP_LOGW(TAG, "No subscription activity detected after %u seconds, falling back to polling mode", 
             SUBSCRIPTION_TIMEOUT_MS / 1000);
    
    // Destroy the subscription bridge
    mqtt_bridge_destroy(&this->mqtt_bridge_);
    
    // Initialize polling bridge
    mqtt_bridge_polling_init(
      &this->mqtt_bridge_polling_,
      &this->timer_group_,
      &this->erd_client_.interface,
      &this->mqtt_client_adapter_.interface,
      this->polling_interval_ms_);
    
    // Mark that we're no longer in subscription mode
    this->subscription_mode_active_ = false;
    
    ESP_LOGI(TAG, "Successfully switched to polling mode");
  }
}

void GeappliancesBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "GE Appliances Bridge:");
  if (!this->configured_device_id_.empty()) {
    ESP_LOGCONFIG(TAG, "  Configured Device ID: %s", this->configured_device_id_.c_str());
  }
  if (!this->final_device_id_.empty()) {
    ESP_LOGCONFIG(TAG, "  Device ID: %s", this->final_device_id_.c_str());
  }
  if (!this->generated_device_id_.empty()) {
    ESP_LOGCONFIG(TAG, "  Generated Device ID: %s", this->generated_device_id_.c_str());
    ESP_LOGCONFIG(TAG, "    Appliance Type: %u", this->appliance_type_);
    ESP_LOGCONFIG(TAG, "    Model Number: %s", this->model_number_.c_str());
    ESP_LOGCONFIG(TAG, "    Serial Number: %s", this->serial_number_.c_str());
  }
  if (this->device_id_state_ == DEVICE_ID_STATE_FAILED) {
    ESP_LOGCONFIG(TAG, "  Device ID Generation: FAILED (see logs for details)");
  }
  ESP_LOGCONFIG(TAG, "  Client Address: 0x%02X", this->client_address_);
  ESP_LOGCONFIG(TAG, "  UART Baud Rate: %lu", gea3_baud);
  
  // Display mode
  const char* mode_str = "Unknown";
  if (this->mode_ == BRIDGE_MODE_POLL) {
    mode_str = "Polling";
  } else if (this->mode_ == BRIDGE_MODE_SUBSCRIBE) {
    mode_str = "Subscription";
  } else if (this->mode_ == BRIDGE_MODE_AUTO) {
    if (this->subscription_mode_active_) {
      mode_str = "Auto (Subscription)";
    } else {
      mode_str = "Auto (Polling - fallback)";
    }
  }
  ESP_LOGCONFIG(TAG, "  Mode: %s", mode_str);
  
  if (this->mode_ == BRIDGE_MODE_POLL || !this->subscription_mode_active_) {
    ESP_LOGCONFIG(TAG, "  Polling Interval: %u ms", this->polling_interval_ms_);
  }
  
  // Display GEA2 configuration if enabled
  if (this->gea2_enabled_) {
    ESP_LOGCONFIG(TAG, "GEA2 Bridge:");
    ESP_LOGCONFIG(TAG, "  Enabled: YES");
    if (!this->gea2_device_id_.empty()) {
      ESP_LOGCONFIG(TAG, "  Device ID: %s", this->gea2_device_id_.c_str());
    } else {
      ESP_LOGCONFIG(TAG, "  Device ID: Auto-generated (gea2_<device>)");
    }
    ESP_LOGCONFIG(TAG, "  UART Baud Rate: %lu", gea2_baud);
    ESP_LOGCONFIG(TAG, "  Polling Interval: %u ms", this->gea2_polling_interval_ms_);
  } else {
    ESP_LOGCONFIG(TAG, "GEA2 Bridge: Disabled (no gea2_uart_id configured)");
  }
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after UART (priority 600) and MQTT (priority 50)
  return setup_priority::DATA;  // Priority 600
}

void GeappliancesBridge::initialize_gea2_bridge_() {
  if (this->gea2_bridge_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "Initializing GEA2 MQTT bridge");
  
  // Determine device ID for GEA2
  std::string gea2_device_id;
  if (!this->gea2_device_id_.empty()) {
    gea2_device_id = this->gea2_device_id_;
  } else if (!this->final_device_id_.empty()) {
    // Use GEA3 device ID with gea2_ prefix
    gea2_device_id = "gea2_" + this->final_device_id_;
  } else {
    // Fallback to generic name
    gea2_device_id = "gea2_appliance";
  }
  
  ESP_LOGI(TAG, "GEA2 Device ID: %s", gea2_device_id.c_str());
  
  // Initialize GEA2 MQTT client adapter
  esphome_mqtt_client_adapter_init(&this->gea2_mqtt_client_adapter_, gea2_device_id.c_str());
  
  // Initialize GEA2 MQTT bridge
  this->gea2_mqtt_bridge_.init(
    &this->timer_group_,
    &this->gea2_erd_client_.interface,
    &this->gea2_mqtt_client_adapter_.interface);
  
  this->gea2_bridge_initialized_ = true;
  ESP_LOGI(TAG, "GEA2 MQTT bridge initialized successfully");
}

}  // namespace geappliances_bridge
}  // namespace esphome
