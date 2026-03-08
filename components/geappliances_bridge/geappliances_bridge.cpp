#include "geappliances_bridge.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome_time_source.h"

namespace esphome {
namespace geappliances_bridge {

static const char *const TAG = "geappliances_bridge";

static const tiny_gea3_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};

// GEA2 ERD client: one attempt per bridge-level retry cycle.
// request_retries = 0 means the ERD client sends exactly one copy of each
// request and fails cleanly after request_timeout ms, rather than queueing
// up to 11 copies in the GEA2 interface's send queue.  Multiple queued copies
// cause half-duplex collisions: the GEA2 interface starts sending a retry at
// the same time the appliance's response to the previous request arrives on
// the bus; the response bytes are treated as unexpected reflections in
// state_send, handle_send_failure() fires, and state_collision_cooldown
// silently discards the response — so no ACK is ever sent.  With retries=0,
// only one packet is ever in-flight at a time, eliminating the collision.
// Bridge-level retries (try_read_erd_with_retry_) are spaced ~500 ms apart
// (200 ms tight loop + 50 ms ESPHome gap + processing), giving appliances
// with slow first-access NVRAM lookups time to cache the value before the
// next attempt.
static const tiny_gea2_erd_client_configuration_t gea2_client_configuration = {
  .request_timeout = 250,
  .request_retries = 0
};

// Tick-counter time source for the GEA2 interface's internal timer group.
// The counter is incremented once per real millisecond inside the GEA2 tight
// loop so that tiny_gea2_interface's internal timers advance by at most 1 ms
// per event regardless of the ~50 ms ESPHome framework gap between loop() calls
// (see doc/geappliances_bridge.md §13 for the full explanation).
// Kept as file-scope statics so the tight-loop code and the tick function
// below can both access them without exposing them as class members.
static tiny_time_source_ticks_t s_gea2_tick_count = 0;
// Tracks the last millis() value at which the GEA2 msec interrupt was fired.
// Initialized to 0 (sentinel: "not yet started"); set to millis() on the first
// entry into the GEA2 tight loop so accumulated boot time is not replayed.
static uint32_t s_gea2_last_ms = 0;

static tiny_time_source_ticks_t gea2_tick_ticks(i_tiny_time_source_t *)
{
  return s_gea2_tick_count;
}
static const i_tiny_time_source_api_t kGea2TickApi = { gea2_tick_ticks };
static i_tiny_time_source_t g_gea2_tick_source = { &kGea2TickApi };

// ERD identifiers for device ID generation and discovery broadcasts
static constexpr tiny_erd_t ERD_MODEL_NUMBER = 0x0001;
static constexpr tiny_erd_t ERD_SERIAL_NUMBER = 0x0002;
static constexpr tiny_erd_t ERD_APPLIANCE_TYPE = 0x0008;
static constexpr uint8_t GEA_BROADCAST_ADDRESS = 0xFF;

void GeappliancesBridge::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

  // If BLE provisioning is enabled, try to load MQTT credentials from NVS.
  // This must happen before the MQTT component's setup() so that any stored
  // credentials override the compile-time secrets.
  if (this->ble_provisioning_enabled_) {
    if (this->load_mqtt_credentials_from_nvs_()) {
      this->apply_nvs_mqtt_credentials_();
    } else {
      ESP_LOGI(TAG, "BLE provisioning enabled: no MQTT credentials in NVS. "
               "Use the 'configure_mqtt' API service to provision credentials.");
    }
  }

  // Initialize timer group
  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

  // Initialize GEA3 components if GEA3 UART is configured
  if (this->uart_ != nullptr) {
    esphome_uart_adapter_init(&this->uart_adapter_, &this->timer_group_, this->uart_);

    tiny_gea3_interface_init(
      &this->gea3_interface_,
      &this->uart_adapter_.interface,
      this->client_address_,
      this->send_queue_buffer_,
      sizeof(this->send_queue_buffer_),
      this->receive_buffer_,
      sizeof(this->receive_buffer_),
      false);

    tiny_gea3_erd_client_init(
      &this->erd_client_,
      &this->timer_group_,
      &this->gea3_interface_.interface,
      this->client_queue_buffer_,
      sizeof(this->client_queue_buffer_),
      &client_configuration);

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
  }

  // Initialize GEA2 components if GEA2 UART is configured
  if (this->gea2_uart_ != nullptr) {
    esphome_uart_adapter_init(&this->gea2_uart_adapter_, &this->timer_group_, this->gea2_uart_);

    // Initialize the GEA2 msec-interrupt event that drives the GEA2 interface's
    // internal timeout counters.  The event is published manually inside the
    // GEA2 tight loop (see loop()) so it only fires when GEA2 is actually
    // in use — keeping the shared timer_group_ free of a 1 ms periodic timer
    // that would starve GEA3/polling-bridge timers when GEA3 is active.
    tiny_event_init(&this->gea2_msec_interrupt_);

    tiny_gea2_interface_init(
      &this->gea2_interface_,
      &this->gea2_uart_adapter_.interface,
      &g_gea2_tick_source,
      &this->gea2_msec_interrupt_.interface,
      this->client_address_,
      this->gea2_send_queue_buffer_,
      sizeof(this->gea2_send_queue_buffer_),
      this->gea2_receive_buffer_,
      sizeof(this->gea2_receive_buffer_),
      false,
      1);

    tiny_gea2_erd_client_init(
      &this->gea2_erd_client_,
      &this->timer_group_,
      &this->gea2_interface_.interface,
      this->gea2_client_queue_buffer_,
      sizeof(this->gea2_client_queue_buffer_),
      &gea2_client_configuration);

    // Initialize the GEA2-to-GEA3 adapter and subscribe to its activity
    gea2_erd_client_adapter_init(&this->gea2_erd_client_adapter_, &this->gea2_erd_client_.interface);

    tiny_event_subscription_init(
      &this->gea2_activity_subscription_,
      this,
      +[](void* context, const void* args) {
        auto self = reinterpret_cast<GeappliancesBridge*>(context);
        auto activity_args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
        self->handle_erd_client_activity_(activity_args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(&this->gea2_erd_client_adapter_.interface),
      &this->gea2_activity_subscription_);
  }

  // Device ID will be set after autodiscovery completes (either configured or autogenerated)
  if (!this->configured_device_id_.empty()) {
    ESP_LOGI(TAG, "Device ID configured: %s (will be applied after autodiscovery)", this->configured_device_id_.c_str());
  } else {
    ESP_LOGI(TAG, "No device_id configured, will auto-generate after autodiscovery");
  }
  // device_id_state_ stays IDLE until autodiscovery completes

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

  // Run the protocol stack. When GEA2 is active, run in a 200 ms wall-clock tight
  // loop so the full TX→RX cycle at 19200 baud completes within a single loop()
  // call (see doc/geappliances_bridge.md §13 for details). Otherwise run a single pass.
  bool need_gea2_loop = this->gea2_uart_ != nullptr && (
    this->gea2_protocol_active_ ||
    this->autodiscovery_state_ == AUTODISCOVERY_GEA2_BROADCAST_PENDING ||
    this->autodiscovery_state_ == AUTODISCOVERY_GEA2_BROADCAST_WAITING);

  if (need_gea2_loop) {
    uint32_t loop_start_ms = millis();
    // Initialize s_gea2_last_ms on first entry so we don't replay accumulated
    // boot time as thousands of spurious msec interrupts.
    if (s_gea2_last_ms == 0) {
      s_gea2_last_ms = loop_start_ms;
    }
    while (millis() - loop_start_ms < GEA2_LOOP_DURATION_MS) {
      // Fire the GEA2 msec interrupt once for each elapsed real millisecond.
      // Doing this here (rather than via a timer_group_ periodic timer) ensures
      // the 1 ms interrupt never fires in the GEA3 single-pass path, preventing
      // starvation of the GEA3/polling-bridge timers in the shared timer_group_
      // when both UARTs are configured but only GEA3 is active.
      uint32_t now_ms = millis();
      while (s_gea2_last_ms < now_ms) {
        s_gea2_tick_count++;
        tiny_event_publish(&this->gea2_msec_interrupt_, nullptr);
        s_gea2_last_ms++;
      }
      tiny_timer_group_run(&this->timer_group_);
      tiny_gea2_interface_run(&this->gea2_interface_);
    }
  } else {
    // Standard single-pass for GEA3 (or while waiting for discovery)
    tiny_timer_group_run(&this->timer_group_);
    if (this->uart_ != nullptr) {
      tiny_gea3_interface_run(&this->gea3_interface_);
    }
  }

  // Run autodiscovery state machine
  this->run_autodiscovery_();

  // Initialize MQTT bridge only after autodiscovery completes, device ID is ready,
  // and MQTT is connected. Autodiscovery must finish first so active_erd_client_
  // and host_address_ are set to the correct appliance — without this guard the
  // bridge can start polling/subscribing before the appliance is found.
  if (this->bridge_init_state_ == BRIDGE_INIT_STATE_WAITING_FOR_MQTT && 
      this->autodiscovery_state_ == AUTODISCOVERY_COMPLETE &&
      mqtt_client != nullptr && mqtt_client->is_connected()) {
    ESP_LOGI(TAG, "Device ID ready and MQTT connected, initializing MQTT bridge");
    this->initialize_mqtt_bridge_();
    this->bridge_init_state_ = BRIDGE_INIT_STATE_COMPLETE;
  }

  // Check for subscription activity timeout in auto mode
  if (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_) {
    this->check_subscription_activity_();
  }

  // Device ID generation: use active_erd_client_ regardless of protocol
  if (this->active_erd_client_ != nullptr) {
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

    case AUTODISCOVERY_WAITING_5S:
      // Note: Unsigned subtraction wraps correctly even when millis() overflows after ~49 days
      if (millis() - this->autodiscovery_timer_start_ >= STARTUP_DELAY_MS) {
        ESP_LOGI(TAG, "5s delay complete, starting autodiscovery");
        // Try GEA3 first if configured, otherwise GEA2
        if (this->uart_ != nullptr) {
          this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
        } else {
          this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
        }
      }
      break;

    case AUTODISCOVERY_GEA3_BROADCAST_PENDING: {
      this->gea3_board_discovered_ = false;
      tiny_gea3_erd_client_request_id_t req_id;
      if (tiny_gea3_erd_client_read(&this->erd_client_.interface, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_APPLIANCE_TYPE)) {
        ESP_LOGI(TAG, "Sent GEA3 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_APPLIANCE_TYPE, GEA_BROADCAST_ADDRESS);
        this->autodiscovery_timer_start_ = millis();
        this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_WAITING;
      }
      // else: retry next loop iteration
      break;
    }

    case AUTODISCOVERY_GEA3_BROADCAST_WAITING:
      if (millis() - this->autodiscovery_timer_start_ >= AUTODISCOVERY_BROADCAST_WINDOW_MS) {
        if (this->gea3_board_discovered_) {
          // host_address_ already set by first responder in handler
          ESP_LOGI(TAG, "GEA3 board discovered at 0x%02X, autodiscovery complete", this->host_address_);
          this->autodiscovery_state_ = AUTODISCOVERY_COMPLETE;
          this->start_device_id_generation_();
        } else if (this->gea2_uart_ != nullptr) {
          // GEA3 failed, try GEA2
          ESP_LOGW(TAG, "No GEA3 boards found, trying GEA2...");
          this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
        } else {
          // Only GEA3 configured, retry
          ESP_LOGW(TAG, "No GEA3 boards found, retrying GEA3...");
          this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
        }
      }
      break;

    case AUTODISCOVERY_GEA2_BROADCAST_PENDING: {
      this->gea2_board_discovered_ = false;
      tiny_gea2_erd_client_request_id_t req_id;
      if (tiny_gea2_erd_client_read(&this->gea2_erd_client_.interface, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_APPLIANCE_TYPE)) {
        ESP_LOGI(TAG, "Sent GEA2 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_APPLIANCE_TYPE, GEA_BROADCAST_ADDRESS);
        this->autodiscovery_timer_start_ = millis();
        this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_WAITING;
      }
      // else: retry next loop iteration
      break;
    }

    case AUTODISCOVERY_GEA2_BROADCAST_WAITING:
      if (millis() - this->autodiscovery_timer_start_ >= AUTODISCOVERY_BROADCAST_WINDOW_MS) {
        if (this->gea2_board_discovered_) {
          // host_address_ already set by first responder in handler
          ESP_LOGI(TAG, "GEA2 board discovered at 0x%02X, autodiscovery complete", this->host_address_);
          this->autodiscovery_state_ = AUTODISCOVERY_COMPLETE;
          this->gea2_protocol_active_ = true;
          this->start_device_id_generation_();
        } else if (this->uart_ != nullptr) {
          // Both configured, retry GEA3 next
          ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA3...");
          this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
        } else {
          // Only GEA2 configured, retry
          ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA2...");
          this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
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
    // Device ID is configured, use it now that autodiscovery is complete
    ESP_LOGI(TAG, "Using configured device_id: %s", this->configured_device_id_.c_str());
    this->final_device_id_ = this->configured_device_id_;
    this->device_id_state_ = DEVICE_ID_STATE_COMPLETE;
    this->bridge_init_state_ = BRIDGE_INIT_STATE_WAITING_FOR_MQTT;
    return;
  }
  const char* protocol = this->gea2_protocol_active_ ? "GEA2" : "GEA3";
  ESP_LOGI(TAG, "Starting device ID generation from host address 0x%02X via %s",
           this->host_address_, protocol);
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

  // Start the 5s autodiscovery delay if not already started
  if (this->autodiscovery_state_ == AUTODISCOVERY_WAITING_FOR_MQTT) {
    ESP_LOGI(TAG, "MQTT connected, waiting %u seconds before autodiscovery", STARTUP_DELAY_MS / 1000);
    this->autodiscovery_timer_start_ = millis();
    this->autodiscovery_state_ = AUTODISCOVERY_WAITING_5S;
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
  // Track subscription activity for auto mode (GEA3 only)
  if (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_ &&
      this->mqtt_bridge_initialized_ &&
      args->address == this->host_address_ &&
      args->type == tiny_gea3_erd_client_activity_type_subscription_publication_received) {
    if (!this->subscription_activity_detected_) {
      ESP_LOGI(TAG, "Subscription activity detected - subscription mode is working");
      this->subscription_activity_detected_ = true;
    }
  }

  // Handle autodiscovery: first responder on GEA3 or GEA2 broadcast
  bool in_gea3_discovery = (this->autodiscovery_state_ == AUTODISCOVERY_GEA3_BROADCAST_WAITING);
  bool in_gea2_discovery = (this->autodiscovery_state_ == AUTODISCOVERY_GEA2_BROADCAST_WAITING);
  if (in_gea3_discovery || in_gea2_discovery) {
    bool& discovered = in_gea3_discovery ? this->gea3_board_discovered_ : this->gea2_board_discovered_;
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed &&
        args->read_completed.erd == ERD_APPLIANCE_TYPE &&
        !discovered && args->read_completed.data_size >= 1) {
      uint8_t app_type = reinterpret_cast<const uint8_t*>(args->read_completed.data)[0];
      if (in_gea3_discovery) {
        ESP_LOGD(TAG, "GEA3 board discovered: address=0x%02X appliance_type=%u (%s)",
                 args->address, app_type, appliance_type_to_string(app_type).c_str());
        this->active_erd_client_ = &this->erd_client_.interface;
      } else {
        ESP_LOGD(TAG, "GEA2 board discovered: address=0x%02X appliance_type=%u (%s)",
                 args->address, app_type, appliance_type_to_string(app_type).c_str());
        this->active_erd_client_ = &this->gea2_erd_client_adapter_.interface;
      }
      discovered = true;
      this->host_address_ = args->address;
    }
    return;
  }

  // Device ID reads (after discovery, before bridge init)
  if (!this->mqtt_bridge_initialized_ && args->address == this->host_address_) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed) {
      this->process_device_id_erd_response_(
        args->read_completed.erd,
        reinterpret_cast<const uint8_t*>(args->read_completed.data),
        args->read_completed.data_size);
    } else if (args->type == tiny_gea3_erd_client_activity_type_read_failed) {
      ESP_LOGW(TAG, "Failed to read ERD 0x%04X for device ID generation (reason: %u), will retry",
               args->read_failed.erd, args->read_failed.reason);
      this->handle_device_id_read_failure_(args->read_failed.erd);
    }
  }
}

void GeappliancesBridge::process_device_id_erd_response_(tiny_erd_t erd, const uint8_t* data, uint8_t size) {
  if (erd == ERD_APPLIANCE_TYPE) {
    if (size < 1) return;
    this->appliance_type_ = data[0];
    ESP_LOGI(TAG, "Read appliance type: %u", this->appliance_type_);
    // Queue the model-number read immediately, while still inside the GEA2
    // tight-loop callback chain.  This mirrors what the polling bridge does in
    // state_add_common_erds and ensures the request is sent within the same
    // 200 ms tight-loop window so the large (32-byte) response can be received
    // before the window expires.  Deferring to the next loop() call crosses the
    // ~50 ms ESPHome overhead gap which can cause timing issues for large ERDs.
    if (this->active_erd_client_ != nullptr &&
        tiny_gea3_erd_client_read(this->active_erd_client_, &this->pending_request_id_,
                                   this->host_address_, ERD_MODEL_NUMBER)) {
      this->device_id_state_ = DEVICE_ID_STATE_IDLE;  // waiting for model-number response
    } else {
      // Fallback: loop() will retry on the next call
      this->device_id_state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
    }
  } else if (erd == ERD_MODEL_NUMBER) {
    this->model_number_ = this->bytes_to_string_(data, size);
    ESP_LOGI(TAG, "Read model number: %s", this->model_number_.c_str());
    // Queue the serial-number read immediately (same reasoning as above).
    if (this->active_erd_client_ != nullptr &&
        tiny_gea3_erd_client_read(this->active_erd_client_, &this->pending_request_id_,
                                   this->host_address_, ERD_SERIAL_NUMBER)) {
      this->device_id_state_ = DEVICE_ID_STATE_IDLE;  // waiting for serial-number response
    } else {
      this->device_id_state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
    }
  } else if (erd == ERD_SERIAL_NUMBER) {
    this->serial_number_ = this->bytes_to_string_(data, size);
    ESP_LOGI(TAG, "Read serial number: %s", this->serial_number_.c_str());

    std::string appliance_type_name = appliance_type_to_string(this->appliance_type_);
    this->generated_device_id_ = appliance_type_name + "_" +
                                 this->sanitize_for_mqtt_topic_(this->model_number_) + "_" +
                                 this->sanitize_for_mqtt_topic_(this->serial_number_);
    this->final_device_id_ = this->generated_device_id_;
    ESP_LOGI(TAG, "Generated device ID: %s", this->final_device_id_.c_str());

    this->device_id_state_ = DEVICE_ID_STATE_COMPLETE;
    this->bridge_init_state_ = BRIDGE_INIT_STATE_WAITING_FOR_MQTT;
  }
}

void GeappliancesBridge::handle_device_id_read_failure_(tiny_erd_t erd) {
  if (erd == ERD_APPLIANCE_TYPE) {
    this->device_id_state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
  } else if (erd == ERD_MODEL_NUMBER) {
    this->device_id_state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
  } else if (erd == ERD_SERIAL_NUMBER) {
    this->device_id_state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
  }
}

void GeappliancesBridge::initialize_mqtt_bridge_() {
  if (this->mqtt_bridge_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "Initializing MQTT bridge with device ID: %s", this->final_device_id_.c_str());

  // active_erd_client_ is set during autodiscovery. For manual device_id configs
  // where autodiscovery is skipped, fall back to GEA3 (or GEA2 if only GEA2 configured).
  if (this->active_erd_client_ == nullptr) {
    if (this->uart_ != nullptr) {
      this->active_erd_client_ = &this->erd_client_.interface;
    } else {
      this->active_erd_client_ = &this->gea2_erd_client_adapter_.interface;
      this->gea2_protocol_active_ = true;
    }
  }

  bool use_polling = false;
  const char* mode_name = "unknown";

  // GEA2 does not support subscriptions; force polling mode
  if (this->gea2_protocol_active_) {
    use_polling = true;
    mode_name = "polling (GEA2 - subscriptions not supported)";
  } else if (this->mode_ == BRIDGE_MODE_POLL) {
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
      this->active_erd_client_,
      &this->mqtt_client_adapter_.interface,
      this->polling_interval_ms_,
      this->polling_only_publish_on_change_);
  } else {
    mqtt_bridge_init(
      &this->mqtt_bridge_,
      &this->timer_group_,
      this->active_erd_client_,
      &this->mqtt_client_adapter_.interface,
      this->host_address_);
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
  if (tiny_gea3_erd_client_read(this->active_erd_client_, &this->pending_request_id_,
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
      this->active_erd_client_,
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
  if (this->uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA3 UART: configured (baud %lu)", baud);
  }
  if (this->gea2_uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA2 UART: configured (baud %u)", 19200u);
  }
  if (this->autodiscovery_state_ == AUTODISCOVERY_COMPLETE) {
    ESP_LOGCONFIG(TAG, "  Active Protocol: %s", this->gea2_protocol_active_ ? "GEA2" : "GEA3");
  }

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
  ESP_LOGCONFIG(TAG, "  BLE Provisioning: %s", this->ble_provisioning_enabled_ ? "enabled" : "disabled");
  if (this->ble_provisioning_enabled_ && this->nvs_credentials_.magic == MQTTNvsCredentials::MAGIC) {
    ESP_LOGCONFIG(TAG, "  MQTT Credentials Source: NVS (broker=%s port=%u)",
                  this->nvs_credentials_.broker, this->nvs_credentials_.port);
  }
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after UART (priority 600) and MQTT (priority 50)
  return setup_priority::DATA;  // Priority 600
}

// NVS preference key – must be unique per component instance.
// Using a fixed hash to avoid a run-time hash computation.
static constexpr uint32_t MQTT_NVS_PREF_HASH = 0x47454150; // "GEAP"

bool GeappliancesBridge::load_mqtt_credentials_from_nvs_() {
  MQTTNvsCredentials creds;
  auto pref = global_preferences->make_preference<MQTTNvsCredentials>(MQTT_NVS_PREF_HASH);
  if (!pref.load(&creds)) {
    return false;
  }
  if (creds.magic != MQTTNvsCredentials::MAGIC) {
    return false;
  }
  // Null-terminate for safety (make_preference loads raw bytes)
  creds.broker[sizeof(creds.broker) - 1] = '\0';
  creds.username[sizeof(creds.username) - 1] = '\0';
  creds.password[sizeof(creds.password) - 1] = '\0';
  this->nvs_credentials_ = creds;
  ESP_LOGI(TAG, "Loaded MQTT credentials from NVS: broker=%s port=%u", creds.broker, creds.port);
  return true;
}

void GeappliancesBridge::apply_nvs_mqtt_credentials_() {
  auto mqtt_client = mqtt::global_mqtt_client;
  if (mqtt_client == nullptr) {
    ESP_LOGW(TAG, "Cannot apply NVS credentials: MQTT client not available");
    return;
  }
  mqtt_client->set_broker_address(this->nvs_credentials_.broker);
  mqtt_client->set_broker_port(this->nvs_credentials_.port);
  mqtt_client->set_username(this->nvs_credentials_.username);
  mqtt_client->set_password(this->nvs_credentials_.password);
  ESP_LOGI(TAG, "Applied NVS MQTT credentials: broker=%s port=%u",
           this->nvs_credentials_.broker, this->nvs_credentials_.port);
}

void GeappliancesBridge::configure_mqtt_credentials(
    const std::string &broker, uint16_t port,
    const std::string &username, const std::string &password) {
  if (broker.empty()) {
    ESP_LOGE(TAG, "configure_mqtt_credentials: broker must not be empty");
    return;
  }
  if (broker.length() >= sizeof(MQTTNvsCredentials::broker)) {
    ESP_LOGE(TAG, "configure_mqtt_credentials: broker too long (max %zu chars)",
             sizeof(MQTTNvsCredentials::broker) - 1);
    return;
  }
  if (username.length() >= sizeof(MQTTNvsCredentials::username) ||
      password.length() >= sizeof(MQTTNvsCredentials::password)) {
    ESP_LOGE(TAG, "configure_mqtt_credentials: username or password too long");
    return;
  }

  MQTTNvsCredentials creds = {};
  strncpy(creds.broker,   broker.c_str(),   sizeof(creds.broker) - 1);
  creds.broker[sizeof(creds.broker) - 1] = '\0';
  strncpy(creds.username, username.c_str(), sizeof(creds.username) - 1);
  creds.username[sizeof(creds.username) - 1] = '\0';
  strncpy(creds.password, password.c_str(), sizeof(creds.password) - 1);
  creds.password[sizeof(creds.password) - 1] = '\0';
  creds.port  = port;
  creds.magic = MQTTNvsCredentials::MAGIC;

  auto pref = global_preferences->make_preference<MQTTNvsCredentials>(MQTT_NVS_PREF_HASH);
  pref.save(&creds);

  ESP_LOGI(TAG, "MQTT credentials saved to NVS (broker=%s port=%u). Rebooting...",
           broker.c_str(), port);

  // A reboot ensures the MQTT component picks up the new credentials cleanly
  // from setup_priority ordering rather than a mid-session reconnect.
  App.safe_reboot();
}

}  // namespace geappliances_bridge
}  // namespace esphome
