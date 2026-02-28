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

static const tiny_gea2_erd_client_configuration_t gea2_client_configuration = {
  .request_timeout = 250,
  .request_retries = 3
};

// ERD identifiers for device ID generation
static constexpr tiny_erd_t ERD_MODEL_NUMBER = 0x0001;
static constexpr tiny_erd_t ERD_SERIAL_NUMBER = 0x0002;
static constexpr tiny_erd_t ERD_APPLIANCE_TYPE = 0x0008;
// ERD used for discovery broadcasts (appliance type)
static constexpr tiny_erd_t ERD_DISCOVERY = 0x0008;
static constexpr uint8_t GEA_BROADCAST_ADDRESS = 0xFF;
static constexpr uint8_t GEA2_INTERFACE_RETRIES = 3;

static void publish_msec_interrupt(void* context)
{
  auto event = static_cast<tiny_event_t*>(context);
  tiny_event_publish(event, nullptr);
}

void GeappliancesBridge::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

  // Initialize timer group
  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

  // Initialize GEA3 UART adapter
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

  // Initialize GEA3 ERD client
  tiny_gea3_erd_client_init(
    &this->erd_client_,
    &this->timer_group_,
    &this->gea3_interface_.interface,
    this->client_queue_buffer_,
    sizeof(this->client_queue_buffer_),
    &client_configuration);

  // Subscribe to GEA3 ERD client activity
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

  // Initialize GEA2 components if a second UART is configured
  if (this->gea2_uart_ != nullptr) {
    ESP_LOGI(TAG, "GEA2 UART configured, initializing GEA2 interface");

    // Initialize the msec_interrupt event (published every ~1ms to drive GEA2 timers)
    tiny_event_init(&this->msec_interrupt_event_);
    tiny_timer_start_periodic(
      &this->timer_group_,
      &this->gea2_msec_timer_,
      1,
      &this->msec_interrupt_event_,
      publish_msec_interrupt);

    // Initialize GEA2 UART adapter
    esphome_uart_adapter_init(&this->gea2_uart_adapter_, &this->timer_group_, this->gea2_uart_);

    // Initialize GEA2 interface
    tiny_gea2_interface_init(
      &this->gea2_interface_,
      &this->gea2_uart_adapter_.interface,
      esphome_time_source_init(),
      &this->msec_interrupt_event_.interface,
      this->client_address_,
      this->gea2_send_queue_buffer_,
      sizeof(this->gea2_send_queue_buffer_),
      this->gea2_receive_buffer_,
      sizeof(this->gea2_receive_buffer_),
      false,
      GEA2_INTERFACE_RETRIES);

    // Initialize GEA2 ERD client
    tiny_gea2_erd_client_init(
      &this->gea2_erd_client_,
      &this->timer_group_,
      &this->gea2_interface_.interface,
      this->gea2_client_queue_buffer_,
      sizeof(this->gea2_client_queue_buffer_),
      &gea2_client_configuration);

    // Subscribe to GEA2 ERD client activity
    tiny_event_subscription_init(
      &this->gea2_erd_client_activity_subscription_,
      this,
      +[](void* context, const void* args) {
        auto self = reinterpret_cast<GeappliancesBridge*>(context);
        auto activity_args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(args);
        self->handle_gea2_erd_client_activity_(activity_args);
      });
    tiny_event_subscribe(
      tiny_gea2_erd_client_on_activity(&this->gea2_erd_client_.interface),
      &this->gea2_erd_client_activity_subscription_);
  }

  // If device_id is configured, set it immediately; otherwise wait for autodiscovery
  if (!this->configured_device_id_.empty()) {
    ESP_LOGI(TAG, "Using configured device_id: %s", this->configured_device_id_.c_str());
    this->final_device_id_ = this->configured_device_id_;
    this->device_id_state_ = DEVICE_ID_STATE_COMPLETE;
    this->bridge_init_state_ = BRIDGE_INIT_STATE_WAITING_FOR_MQTT;
  } else {
    ESP_LOGI(TAG, "No device_id configured, will auto-generate after autodiscovery");
    // device_id_state_ stays IDLE until autodiscovery completes
  }

  // Autodiscovery starts after MQTT connects (handled in on_mqtt_connected_())
  ESP_LOGI(TAG, "Waiting for MQTT connection before starting autodiscovery...");

  ESP_LOGCONFIG(TAG, "GE Appliances Bridge setup complete");
}

void GeappliancesBridge::loop() {
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

  // Run timer group (always, non-blocking)
  tiny_timer_group_run(&this->timer_group_);
  
  // Run GEA3 interface (always)
  tiny_gea3_interface_run(&this->gea3_interface_);

  // Run GEA2 interface (if configured)
  if (this->gea2_uart_ != nullptr) {
    tiny_gea2_interface_run(&this->gea2_interface_);
  }

  // Run autodiscovery state machine
  this->run_autodiscovery_();

  // Initialize MQTT bridge when device ID is ready and MQTT is connected
  if (this->bridge_init_state_ == BRIDGE_INIT_STATE_WAITING_FOR_MQTT && 
      mqtt_client != nullptr && mqtt_client->is_connected()) {
    ESP_LOGI(TAG, "Device ID ready and MQTT connected, initializing MQTT bridge");
    this->initialize_mqtt_bridge_();
    this->bridge_init_state_ = BRIDGE_INIT_STATE_COMPLETE;
  }

  // Check for subscription activity timeout in auto mode
  if (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_) {
    this->check_subscription_activity_();
  }

  // Handle device ID generation state machine
  // Note: If state reaches DEVICE_ID_STATE_FAILED, device requires reboot to retry
  if (this->use_gea2_for_device_id_) {
    // GEA2 path: read all device ID ERDs via GEA2 client
    tiny_erd_t erd = 0;
    const char* erd_name = nullptr;
    if (this->device_id_state_ == DEVICE_ID_STATE_READING_APPLIANCE_TYPE) {
      erd = ERD_APPLIANCE_TYPE;
      erd_name = "appliance type";
    } else if (this->device_id_state_ == DEVICE_ID_STATE_READING_MODEL_NUMBER) {
      erd = ERD_MODEL_NUMBER;
      erd_name = "model number";
    } else if (this->device_id_state_ == DEVICE_ID_STATE_READING_SERIAL_NUMBER) {
      erd = ERD_SERIAL_NUMBER;
      erd_name = "serial number";
    }
    if (erd_name != nullptr) {
      if (tiny_gea2_erd_client_read(&this->gea2_erd_client_.interface, &this->gea2_pending_request_id_,
                                     this->host_address_, erd)) {
        ESP_LOGD(TAG, "Reading %s ERD 0x%04X via GEA2", erd_name, erd);
        this->device_id_state_ = DEVICE_ID_STATE_IDLE;
        this->read_retry_count_ = 0;
      } else {
        this->read_retry_count_++;
        if (this->read_retry_count_ >= MAX_READ_RETRIES) {
          ESP_LOGE(TAG, "Failed to read %s via GEA2 after %u retries, giving up", erd_name, MAX_READ_RETRIES);
          this->device_id_state_ = DEVICE_ID_STATE_FAILED;
        } else if (this->read_retry_count_ % LOG_EVERY_N_RETRIES == 0) {
          ESP_LOGW(TAG, "Failed to queue %s read via GEA2, retrying... (attempt %u)", erd_name, this->read_retry_count_);
        }
      }
    }
  } else {
    // GEA3 path
    if (this->device_id_state_ == DEVICE_ID_STATE_READING_APPLIANCE_TYPE) {
      this->try_read_erd_with_retry_(ERD_APPLIANCE_TYPE, "appliance type");
    } else if (this->device_id_state_ == DEVICE_ID_STATE_READING_MODEL_NUMBER) {
      this->try_read_erd_with_retry_(ERD_MODEL_NUMBER, "model number");
    } else if (this->device_id_state_ == DEVICE_ID_STATE_READING_SERIAL_NUMBER) {
      this->try_read_erd_with_retry_(ERD_SERIAL_NUMBER, "serial number");
    }
  }
}

void GeappliancesBridge::run_autodiscovery_() {
  switch (this->autodiscovery_state_) {
    case AUTODISCOVERY_WAITING_FOR_MQTT:
      // Handled in on_mqtt_connected_()
      break;

    case AUTODISCOVERY_WAITING_20S:
      // Note: Unsigned subtraction wraps correctly even when millis() overflows after ~49 days
      if (millis() - this->autodiscovery_timer_start_ >= STARTUP_DELAY_MS) {
        ESP_LOGI(TAG, "20s delay complete, starting GEA2/3 autodiscovery");
        if (this->gea_mode_ == GEA_MODE_GEA2) {
          this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
        } else {
          this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
        }
      }
      break;

    case AUTODISCOVERY_GEA3_BROADCAST_PENDING: {
      // Reset GEA3 discovery tracking for this broadcast cycle
      this->gea3_board_discovered_ = false;
      this->gea3_preferred_found_ = false;
      this->gea3_first_address_ = 0x00;
      this->gea3_first_address_set_ = false;
      tiny_gea3_erd_client_request_id_t req_id;
      if (tiny_gea3_erd_client_read(&this->erd_client_.interface, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_DISCOVERY)) {
        ESP_LOGI(TAG, "Sent GEA3 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_DISCOVERY, GEA_BROADCAST_ADDRESS);
        this->autodiscovery_timer_start_ = millis();
        this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_WAITING;
      }
      // else: retry next loop iteration
      break;
    }

    case AUTODISCOVERY_GEA3_BROADCAST_WAITING:
      if (millis() - this->autodiscovery_timer_start_ >= AUTODISCOVERY_BROADCAST_WINDOW_MS) {
        if (this->gea3_board_discovered_) {
          // Use preferred address if found; otherwise use first responder
          if (!this->gea3_preferred_found_) {
            this->host_address_ = this->gea3_first_address_;
          }
          this->use_gea2_for_device_id_ = false;
          ESP_LOGI(TAG, "GEA3 board discovered at 0x%02X, autodiscovery complete", this->host_address_);
          this->autodiscovery_state_ = AUTODISCOVERY_COMPLETE;
          this->start_device_id_generation_();
        } else {
          if (this->gea_mode_ == GEA_MODE_GEA3 ||
              (this->gea_mode_ == GEA_MODE_AUTO && this->gea2_uart_ == nullptr)) {
            ESP_LOGW(TAG, "No GEA3 boards found, retrying GEA3...");
            this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
          } else {
            ESP_LOGI(TAG, "No GEA3 boards found, trying GEA2...");
            this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
          }
        }
      }
      break;

    case AUTODISCOVERY_GEA2_BROADCAST_PENDING:
      if (this->gea2_uart_ == nullptr) {
        ESP_LOGE(TAG, "GEA2 mode selected but no gea2_uart_id configured; falling back to GEA3 autodiscovery");
        this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
      } else {
        // Reset GEA2 discovery tracking for this broadcast cycle
        this->gea2_board_discovered_ = false;
        this->gea2_preferred_found_ = false;
        this->gea2_first_address_ = 0x00;
        this->gea2_first_address_set_ = false;
        tiny_gea2_erd_client_request_id_t req_id;
        if (tiny_gea2_erd_client_read(&this->gea2_erd_client_.interface, &req_id,
                                       GEA_BROADCAST_ADDRESS, ERD_DISCOVERY)) {
          ESP_LOGI(TAG, "Sent GEA2 broadcast (ERD 0x%04X) to address 0x%02X",
                   ERD_DISCOVERY, GEA_BROADCAST_ADDRESS);
          this->autodiscovery_timer_start_ = millis();
          this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_WAITING;
        }
        // else: retry next loop iteration
      }
      break;

    case AUTODISCOVERY_GEA2_BROADCAST_WAITING:
      if (millis() - this->autodiscovery_timer_start_ >= AUTODISCOVERY_BROADCAST_WINDOW_MS) {
        if (this->gea2_board_discovered_) {
          // Use preferred address if found; otherwise use first responder
          if (!this->gea2_preferred_found_) {
            this->host_address_ = this->gea2_first_address_;
          }
          this->use_gea2_for_device_id_ = true;
          ESP_LOGI(TAG, "GEA2 board discovered at 0x%02X, autodiscovery complete", this->host_address_);
          this->autodiscovery_state_ = AUTODISCOVERY_COMPLETE;
          this->start_device_id_generation_();
        } else {
          if (this->gea_mode_ == GEA_MODE_GEA2) {
            ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA2...");
            this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
          } else {
            ESP_LOGW(TAG, "No boards found after GEA3+GEA2 broadcasts, repeating discovery loop...");
            this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
          }
        }
      }
      break;

    case AUTODISCOVERY_COMPLETE:
      break;
  }
}

void GeappliancesBridge::start_device_id_generation_() {
  // Only start device ID generation if it hasn't been set already
  if (this->device_id_state_ != DEVICE_ID_STATE_IDLE) {
    return;
  }
  if (!this->configured_device_id_.empty()) {
    // Device ID already configured - MQTT bridge init handled by bridge_init_state_
    return;
  }
  ESP_LOGI(TAG, "Starting device ID generation from host address 0x%02X via %s",
           this->host_address_, this->use_gea2_for_device_id_ ? "GEA2" : "GEA3");
  this->device_id_state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
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

  // Start the 20s autodiscovery delay if not already started
  if (this->autodiscovery_state_ == AUTODISCOVERY_WAITING_FOR_MQTT) {
    ESP_LOGI(TAG, "MQTT connected, waiting %u seconds before autodiscovery", STARTUP_DELAY_MS / 1000);
    this->autodiscovery_timer_start_ = millis();
    this->autodiscovery_state_ = AUTODISCOVERY_WAITING_20S;
  }
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
        args->address == this->host_address_ &&
        args->type == tiny_gea3_erd_client_activity_type_subscription_publication_received) {
      if (!this->subscription_activity_detected_) {
        ESP_LOGI(TAG, "Subscription activity detected - subscription mode is working");
        this->subscription_activity_detected_ = true;
      }
    }
  }

  // Handle autodiscovery responses (GEA3 broadcast window)
  if (this->autodiscovery_state_ == AUTODISCOVERY_GEA3_BROADCAST_WAITING) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed &&
        args->read_completed.erd == ERD_DISCOVERY) {
      uint8_t app_type = reinterpret_cast<const uint8_t*>(args->read_completed.data)[0];
      std::string app_type_name = appliance_type_to_string(app_type);
      ESP_LOGD(TAG, "GEA3 board discovered: address=0x%02X appliance_type=%u (%s)",
               args->address, app_type, app_type_name.c_str());
      this->gea3_board_discovered_ = true;
      if (args->address == this->gea3_address_preference_) {
        // Preferred address responded - use it for device ID generation
        this->gea3_preferred_found_ = true;
        this->host_address_ = args->address;
        this->use_gea2_for_device_id_ = false;
      } else if (!this->gea3_first_address_set_) {
        // Track first non-preferred responder as fallback
        this->gea3_first_address_ = args->address;
        this->gea3_first_address_set_ = true;
      }
    }
    return; // Don't process as device ID during autodiscovery window
  }

  // Only process read responses for device ID ERDs before MQTT bridge is initialized
  if (!this->mqtt_bridge_initialized_ && args->address == this->host_address_) {
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

void GeappliancesBridge::handle_gea2_erd_client_activity_(const tiny_gea2_erd_client_on_activity_args_t* args) {
  // Handle autodiscovery responses (GEA2 broadcast window)
  if (this->autodiscovery_state_ == AUTODISCOVERY_GEA2_BROADCAST_WAITING) {
    if (args->type == tiny_gea2_erd_client_activity_type_read_completed &&
        args->read_completed.erd == ERD_DISCOVERY) {
      uint8_t app_type = reinterpret_cast<const uint8_t*>(args->read_completed.data)[0];
      std::string app_type_name = appliance_type_to_string(app_type);
      ESP_LOGD(TAG, "GEA2 board discovered: address=0x%02X appliance_type=%u (%s)",
               args->address, app_type, app_type_name.c_str());
      this->gea2_board_discovered_ = true;
      if (args->address == this->gea2_address_preference_) {
        // Preferred address responded - use it for device ID generation
        this->gea2_preferred_found_ = true;
        this->host_address_ = args->address;
        this->use_gea2_for_device_id_ = true;
      } else if (!this->gea2_first_address_set_) {
        // Track first non-preferred responder as fallback
        this->gea2_first_address_ = args->address;
        this->gea2_first_address_set_ = true;
      }
    }
    return; // Don't process as device ID during autodiscovery window
  }

  // Handle device ID reads via GEA2 (when use_gea2_for_device_id_ is true)
  if (this->use_gea2_for_device_id_ && !this->mqtt_bridge_initialized_ &&
      args->address == this->host_address_) {
    if (args->type == tiny_gea2_erd_client_activity_type_read_completed) {
      if (args->read_completed.erd == ERD_APPLIANCE_TYPE) {
        this->appliance_type_ = reinterpret_cast<const uint8_t*>(args->read_completed.data)[0];
        ESP_LOGI(TAG, "Read appliance type via GEA2: %u", this->appliance_type_);
        this->device_id_state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
      } else if (args->read_completed.erd == ERD_MODEL_NUMBER) {
        this->model_number_ = this->bytes_to_string_(
          reinterpret_cast<const uint8_t*>(args->read_completed.data),
          args->read_completed.data_size);
        ESP_LOGI(TAG, "Read model number via GEA2: %s", this->model_number_.c_str());
        this->device_id_state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
      } else if (args->read_completed.erd == ERD_SERIAL_NUMBER) {
        this->serial_number_ = this->bytes_to_string_(
          reinterpret_cast<const uint8_t*>(args->read_completed.data),
          args->read_completed.data_size);
        ESP_LOGI(TAG, "Read serial number via GEA2: %s", this->serial_number_.c_str());

        std::string sanitized_model = this->sanitize_for_mqtt_topic_(this->model_number_);
        std::string sanitized_serial = this->sanitize_for_mqtt_topic_(this->serial_number_);
        std::string appliance_type_name = appliance_type_to_string(this->appliance_type_);

        this->generated_device_id_ = appliance_type_name + "_" +
                                     sanitized_model + "_" +
                                     sanitized_serial;
        this->final_device_id_ = this->generated_device_id_;
        ESP_LOGI(TAG, "Generated device ID (via GEA2): %s", this->final_device_id_.c_str());

        this->device_id_state_ = DEVICE_ID_STATE_COMPLETE;
        this->bridge_init_state_ = BRIDGE_INIT_STATE_WAITING_FOR_MQTT;
      }
    } else if (args->type == tiny_gea2_erd_client_activity_type_read_failed) {
      ESP_LOGW(TAG, "Failed to read ERD 0x%04X via GEA2 (reason: %u), will retry",
               args->read_failed.erd, args->read_failed.reason);
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

  // Initialize MQTT bridge based on mode
  if (use_polling) {
    mqtt_bridge_polling_init(
      &this->mqtt_bridge_polling_,
      &this->timer_group_,
      &this->erd_client_.interface,
      &this->mqtt_client_adapter_.interface,
      this->polling_interval_ms_,
      this->polling_only_publish_on_change_);
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
                                 this->host_address_, erd)) {
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
      this->polling_interval_ms_,
      this->polling_only_publish_on_change_);
    
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
  ESP_LOGCONFIG(TAG, "  Host Address: 0x%02X", this->host_address_);
  ESP_LOGCONFIG(TAG, "  GEA3 UART Baud Rate: %lu", baud);
  ESP_LOGCONFIG(TAG, "  GEA3 Preferred Address: 0x%02X", this->gea3_address_preference_);
  if (this->gea2_uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA2 UART: configured");
    ESP_LOGCONFIG(TAG, "  GEA2 Preferred Address: 0x%02X", this->gea2_address_preference_);
  }

  // Display GEA protocol mode
  const char* gea_mode_str = "Unknown";
  if (this->gea_mode_ == GEA_MODE_AUTO) {
    gea_mode_str = "Auto (GEA3 first, then GEA2)";
  } else if (this->gea_mode_ == GEA_MODE_GEA3) {
    gea_mode_str = "GEA3 only";
  } else if (this->gea_mode_ == GEA_MODE_GEA2) {
    gea_mode_str = "GEA2 only";
  }
  ESP_LOGCONFIG(TAG, "  GEA Mode: %s", gea_mode_str);

  // Display bridge mode
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
    ESP_LOGCONFIG(TAG, "  Only Publish On Change: %s", this->polling_only_publish_on_change_ ? "yes" : "no");
  }
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after UART (priority 600) and MQTT (priority 50)
  return setup_priority::DATA;  // Priority 600
}

}  // namespace geappliances_bridge
}  // namespace esphome
