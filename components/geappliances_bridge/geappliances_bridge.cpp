#include "geappliances_bridge.h"
#include "appliance_api_feature_lists.h"
#include "esphome/core/log.h"
#include "esphome_time_source.h"
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

// ERD identifiers for appliance API feature bits
static constexpr tiny_erd_t ERD_COMMON_FEATURE_API = 0x0092;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_0 = 0x0093;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_1 = 0x0094;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_2 = 0x0095;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_3 = 0x0096;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_4 = 0x0097;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_5 = 0x0109;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_6 = 0x010A;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_7 = 0x010B;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_8 = 0x010C;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_9 = 0x010D;

static constexpr uint8_t APPLIANCE_FEATURE_ERD_SIZE = 8; // [2B featureType][2B version][4B bitmap]

// Returns true if the ERD is one of the feature bit ERDs (0x0092-0x0097 and 0x0109-0x010D).
static inline bool is_feature_bit_erd(tiny_erd_t erd) {
  return erd == ERD_COMMON_FEATURE_API ||
         erd == ERD_APPLIANCE_FEATURE_API_0 ||
         erd == ERD_APPLIANCE_FEATURE_API_1 ||
         erd == ERD_APPLIANCE_FEATURE_API_2 ||
         erd == ERD_APPLIANCE_FEATURE_API_3 ||
         erd == ERD_APPLIANCE_FEATURE_API_4 ||
         erd == ERD_APPLIANCE_FEATURE_API_5 ||
         erd == ERD_APPLIANCE_FEATURE_API_6 ||
         erd == ERD_APPLIANCE_FEATURE_API_7 ||
         erd == ERD_APPLIANCE_FEATURE_API_8 ||
         erd == ERD_APPLIANCE_FEATURE_API_9;
}

// Read up to 8 bytes from a big-endian byte buffer as a 64-bit integer.
// GEA protocol transmits ERD values MSB-first (big-endian).
static inline uint64_t read_be64(const uint8_t* buf, uint8_t size) {
  uint64_t bits = 0;
  uint8_t n = (size < 8) ? size : 8;
  for (uint8_t i = 0; i < n; i++) {
    bits = (bits << 8) | buf[i];
  }
  return bits;
}

void GeappliancesBridge::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

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

  // Update checker: handle result from background task and periodic re-check.
  if (this->update_check_done_) {
    this->update_check_done_ = false;
    this->update_check_in_progress_ = false;
    this->on_update_check_complete_();
  }

  // Trigger a periodic re-check every UPDATE_CHECK_INTERVAL_MS.
  // Require MQTT to be connected as a proxy for network availability before
  // attempting the HTTPS request to the GitHub API.
  if (!this->update_check_in_progress_ &&
      mqtt_client != nullptr && mqtt_client->is_connected() &&
      this->last_update_check_ms_ != 0 &&
      (millis() - this->last_update_check_ms_ >= UPDATE_CHECK_INTERVAL_MS)) {
    this->schedule_update_check_();
  }

  // Feature bit reading: runs after autodiscovery, before device ID generation.
  // Uses direct read requests (not try_read_erd_with_retry_) to avoid interfering
  // with device_id_state_. State transitions to IN_FLIGHT when a read is queued,
  // preventing duplicate requests.
  if (this->active_erd_client_ != nullptr) {
    // When the last feature-bit ERD has been processed (in the GEA callback),
    // feature_bit_parse_pending_ is set and parsing is deferred to here so the
    // callback returns quickly and the GEA2 tight-loop can continue processing
    // UART bytes without being stalled by the potentially slow parse_and_log_feature_bits_().
    if (this->feature_bit_state_ == FEATURE_BIT_STATE_COMPLETE && this->feature_bit_parse_pending_) {
      this->feature_bit_parse_pending_ = false;
      this->parse_and_log_feature_bits_();
      this->start_device_id_generation_();
    }

    tiny_erd_t feature_erd = 0;
    const char* feature_name = nullptr;
    FeatureBitState next_in_flight_state = FEATURE_BIT_STATE_IN_FLIGHT;

    if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_0092) {
      feature_erd = ERD_COMMON_FEATURE_API;
      feature_name = "common feature API (0x0092)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_0093) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_0;
      feature_name = "appliance feature API 0 (0x0093)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_0094) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_1;
      feature_name = "appliance feature API 1 (0x0094)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_0095) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_2;
      feature_name = "appliance feature API 2 (0x0095)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_0096) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_3;
      feature_name = "appliance feature API 3 (0x0096)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_0097) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_4;
      feature_name = "appliance feature API 4 (0x0097)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_0109) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_5;
      feature_name = "appliance feature API 5 (0x0109)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_010A) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_6;
      feature_name = "appliance feature API 6 (0x010A)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_010B) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_7;
      feature_name = "appliance feature API 7 (0x010B)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_010C) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_8;
      feature_name = "appliance feature API 8 (0x010C)";
    } else if (this->feature_bit_state_ == FEATURE_BIT_STATE_READING_010D) {
      feature_erd = ERD_APPLIANCE_FEATURE_API_9;
      feature_name = "appliance feature API 9 (0x010D)";
    }

    if (feature_name != nullptr) {
      if (tiny_gea3_erd_client_read(this->active_erd_client_, &this->pending_request_id_,
                                     this->host_address_, feature_erd)) {
        ESP_LOGD(TAG, "Queued read for %s", feature_name);
        this->read_retry_count_ = 0;
        this->feature_bit_state_ = next_in_flight_state;
      } else {
        this->read_retry_count_++;
        if (this->read_retry_count_ >= MAX_READ_RETRIES) {
          ESP_LOGW(TAG, "Could not queue read for %s after %u attempts, skipping ERD",
                   feature_name, MAX_READ_RETRIES);
          this->read_retry_count_ = 0;
          this->skip_to_next_feature_erd_(feature_erd);
        } else if (this->read_retry_count_ % LOG_EVERY_N_RETRIES == 0) {
          ESP_LOGW(TAG, "Failed to queue %s read, retrying... (attempt %u)",
                   feature_name, this->read_retry_count_);
        }
      }
    }
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
          this->start_feature_bit_reading_();
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
          this->start_feature_bit_reading_();
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

void GeappliancesBridge::start_feature_bit_reading_() {
  if (this->feature_bit_state_ != FEATURE_BIT_STATE_IDLE) {
    return;
  }
  ESP_LOGI(TAG, "Reading appliance API feature bits (ERDs 0x0092-0x0097 and 0x0109-0x010D)...");
  this->read_retry_count_ = 0;
  this->feature_bit_state_ = FEATURE_BIT_STATE_READING_0092;
}

void GeappliancesBridge::process_feature_bit_erd_response_(tiny_erd_t erd, const uint8_t* data, uint8_t size) {
  // Clamp to 8 bytes max per ERD
  uint8_t copy_size = (size <= 8) ? size : 8;
  this->read_retry_count_ = 0;

  // Helper lambda: try to immediately queue the next ERD read; if busy, fall
  // back to setting the corresponding READING state so loop() retries.
  auto queue_next = [this](tiny_erd_t next_erd, FeatureBitState fallback_state) {
    if (this->active_erd_client_ != nullptr &&
        tiny_gea3_erd_client_read(this->active_erd_client_, &this->pending_request_id_,
                                   this->host_address_, next_erd)) {
      this->feature_bit_state_ = FEATURE_BIT_STATE_IN_FLIGHT;
    } else {
      this->feature_bit_state_ = fallback_state;
    }
  };

  if (erd == ERD_COMMON_FEATURE_API) {
    memcpy(this->feature_bit_erd_0092_, data, copy_size);
    this->feature_bit_erd_0092_size_ = copy_size;
    ESP_LOGD(TAG, "Read common feature API (0x0092): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_0, FEATURE_BIT_STATE_READING_0093);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_0) {
    memcpy(this->feature_bit_erd_0093_, data, copy_size);
    this->feature_bit_erd_0093_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 0 (0x0093): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_1, FEATURE_BIT_STATE_READING_0094);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_1) {
    memcpy(this->feature_bit_erd_0094_, data, copy_size);
    this->feature_bit_erd_0094_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 1 (0x0094): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_2, FEATURE_BIT_STATE_READING_0095);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_2) {
    memcpy(this->feature_bit_erd_0095_, data, copy_size);
    this->feature_bit_erd_0095_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 2 (0x0095): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_3, FEATURE_BIT_STATE_READING_0096);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_3) {
    memcpy(this->feature_bit_erd_0096_, data, copy_size);
    this->feature_bit_erd_0096_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 3 (0x0096): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_4, FEATURE_BIT_STATE_READING_0097);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_4) {
    memcpy(this->feature_bit_erd_0097_, data, copy_size);
    this->feature_bit_erd_0097_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 4 (0x0097): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_5, FEATURE_BIT_STATE_READING_0109);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_5) {
    memcpy(this->feature_bit_erd_0109_, data, copy_size);
    this->feature_bit_erd_0109_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 5 (0x0109): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_6, FEATURE_BIT_STATE_READING_010A);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_6) {
    memcpy(this->feature_bit_erd_010A_, data, copy_size);
    this->feature_bit_erd_010A_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 6 (0x010A): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_7, FEATURE_BIT_STATE_READING_010B);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_7) {
    memcpy(this->feature_bit_erd_010B_, data, copy_size);
    this->feature_bit_erd_010B_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 7 (0x010B): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_8, FEATURE_BIT_STATE_READING_010C);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_8) {
    memcpy(this->feature_bit_erd_010C_, data, copy_size);
    this->feature_bit_erd_010C_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 8 (0x010C): %u bytes", copy_size);
    queue_next(ERD_APPLIANCE_FEATURE_API_9, FEATURE_BIT_STATE_READING_010D);
  } else if (erd == ERD_APPLIANCE_FEATURE_API_9) {
    memcpy(this->feature_bit_erd_010D_, data, copy_size);
    this->feature_bit_erd_010D_size_ = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 9 (0x010D): %u bytes", copy_size);
    // Defer parsing and device-ID generation to loop() so this callback returns
    // quickly. parse_and_log_feature_bits_() can be slow (many ESP_LOGI calls,
    // std::set insertions) and must not stall the GEA2 tight-loop.
    this->feature_bit_state_ = FEATURE_BIT_STATE_COMPLETE;
    this->feature_bit_parse_pending_ = true;
  }
}

void GeappliancesBridge::handle_feature_bit_read_failure_(tiny_erd_t erd) {
  // On any feature bit ERD read failure (timeout, NACK, or "Not Supported"),
  // skip this slot and advance to the next ERD in the chain. Not all appliances
  // implement every slot, so a failure here is normal and non-fatal.
  ESP_LOGD(TAG, "Feature bit ERD 0x%04X failed or not supported, skipping", erd);
  this->skip_to_next_feature_erd_(erd);
}

void GeappliancesBridge::skip_to_next_feature_erd_(tiny_erd_t failed_erd) {
  // Advance the state machine to the READING state for the ERD that follows
  // failed_erd in the chain. For the last ERD (0x010D), trigger parse +
  // device-ID generation (same path as a successful final read).
  switch (failed_erd) {
    case ERD_COMMON_FEATURE_API:      this->feature_bit_state_ = FEATURE_BIT_STATE_READING_0093; break;
    case ERD_APPLIANCE_FEATURE_API_0: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_0094; break;
    case ERD_APPLIANCE_FEATURE_API_1: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_0095; break;
    case ERD_APPLIANCE_FEATURE_API_2: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_0096; break;
    case ERD_APPLIANCE_FEATURE_API_3: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_0097; break;
    case ERD_APPLIANCE_FEATURE_API_4: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_0109; break;
    case ERD_APPLIANCE_FEATURE_API_5: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_010A; break;
    case ERD_APPLIANCE_FEATURE_API_6: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_010B; break;
    case ERD_APPLIANCE_FEATURE_API_7: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_010C; break;
    case ERD_APPLIANCE_FEATURE_API_8: this->feature_bit_state_ = FEATURE_BIT_STATE_READING_010D; break;
    case ERD_APPLIANCE_FEATURE_API_9:
      // Last ERD in the chain; proceed with whatever data was collected.
      // Defer parsing and device-ID generation to loop() (same as the success path).
      this->feature_bit_state_ = FEATURE_BIT_STATE_COMPLETE;
      this->feature_bit_parse_pending_ = true;
      break;
    default: break;
  }
}

void GeappliancesBridge::parse_and_log_feature_bits_() {
  this->appliance_api_valid_erds_.clear();
  this->appliance_api_valid_erds_vec_.clear();
  this->appliance_api_valid_list_ready_ = false;

  // Parse common features from ERD 0x0092.
  // ERD 0x0092 is a 4-byte big-endian bitmask; each bit corresponds to a common feature.
  if (this->feature_bit_erd_0092_size_ > 0) {
    uint32_t common_bits = static_cast<uint32_t>(
      read_be64(this->feature_bit_erd_0092_, this->feature_bit_erd_0092_size_) & 0xFFFFFFFFu);
    ESP_LOGI(TAG, "Common feature API (0x0092) value: 0x%08X", common_bits);
    for (uint16_t i = 0; i < common_feature_descriptor_count; i++) {
      const auto& desc = common_feature_descriptors[i];
      if (common_bits & desc.bit_mask) {
        ESP_LOGI(TAG, "  [SET] Common feature: %s (mask 0x%08X, %u ERDs)",
                 desc.name, desc.bit_mask, desc.erd_count);
        for (uint16_t j = 0; j < desc.erd_count; j++) {
          this->appliance_api_valid_erds_.insert(desc.erds[j]);
        }
      }
    }
  }

  // Parse appliance feature APIs from ERDs 0x0093-0x0097 and 0x0109-0x010D.
  // Each ERD has the structure: [2B featureType][2B version][4B feature bitmap]
  // featureType identifies the appliance API (e.g. 0x0014 = Zoneline).
  // version selects which version's feature set applies.
  // Each set bit in the feature bitmap enables a specific feature's ERDs.
  const uint8_t* api_bufs[10] = {
    this->feature_bit_erd_0093_,
    this->feature_bit_erd_0094_,
    this->feature_bit_erd_0095_,
    this->feature_bit_erd_0096_,
    this->feature_bit_erd_0097_,
    this->feature_bit_erd_0109_,
    this->feature_bit_erd_010A_,
    this->feature_bit_erd_010B_,
    this->feature_bit_erd_010C_,
    this->feature_bit_erd_010D_
  };
  const uint8_t api_sizes[10] = {
    this->feature_bit_erd_0093_size_,
    this->feature_bit_erd_0094_size_,
    this->feature_bit_erd_0095_size_,
    this->feature_bit_erd_0096_size_,
    this->feature_bit_erd_0097_size_,
    this->feature_bit_erd_0109_size_,
    this->feature_bit_erd_010A_size_,
    this->feature_bit_erd_010B_size_,
    this->feature_bit_erd_010C_size_,
    this->feature_bit_erd_010D_size_
  };
  static const char* const erd_names[10] = {
    "0x0093", "0x0094", "0x0095", "0x0096", "0x0097",
    "0x0109", "0x010A", "0x010B", "0x010C", "0x010D"
  };

  for (uint8_t erd_idx = 0; erd_idx < 10; erd_idx++) {
    if (api_sizes[erd_idx] < APPLIANCE_FEATURE_ERD_SIZE) continue;
    const uint8_t* buf = api_bufs[erd_idx];
    uint16_t appliance_type = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    uint16_t version        = (static_cast<uint16_t>(buf[2]) << 8) | buf[3];
    uint32_t feature_bitmap = (static_cast<uint32_t>(buf[4]) << 24) |
                              (static_cast<uint32_t>(buf[5]) << 16) |
                              (static_cast<uint32_t>(buf[6]) <<  8) |
                               buf[7];
    if (appliance_type == 0 && version == 0 && feature_bitmap == 0) continue;
    ESP_LOGI(TAG, "Appliance feature ERD %s: type 0x%04X, version %u, features 0x%08X",
             erd_names[erd_idx], appliance_type, version, feature_bitmap);
    bool found_matching_descriptor = false;
    for (uint16_t i = 0; i < appliance_feature_api_descriptor_count; i++) {
      const auto& desc = appliance_feature_api_descriptors[i];
      if (desc.feature_type != appliance_type || desc.version != version) continue;
      found_matching_descriptor = true;
      if (feature_bitmap & desc.bit_mask) {
        ESP_LOGI(TAG, "  [SET] %s (mask 0x%08X, %u ERDs)",
                 desc.name, desc.bit_mask, desc.erd_count);
        for (uint16_t j = 0; j < desc.erd_count; j++) {
          this->appliance_api_valid_erds_.insert(desc.erds[j]);
        }
      }
    }
    if (!found_matching_descriptor) {
      ESP_LOGW(TAG, "  No known API definition for type 0x%04X version %u",
               appliance_type, version);
    }
  }

  // Build the sorted vector for passing to the polling bridge
  this->appliance_api_valid_erds_vec_.assign(
    this->appliance_api_valid_erds_.begin(),
    this->appliance_api_valid_erds_.end());

  ESP_LOGI(TAG, "Appliance API feature parsing complete: %zu valid ERDs identified",
           this->appliance_api_valid_erds_vec_.size());
  this->appliance_api_valid_list_ready_ = true;
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

  // Schedule an update check.  We re-schedule on every reconnect so the
  // update state is always refreshed after a network outage.
  this->schedule_update_check_();
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

  // Device ID + feature bit reads (after discovery, before bridge init)
  if (!this->mqtt_bridge_initialized_ && args->address == this->host_address_) {
    // Route based on ERD value: feature bit ERDs go to the feature bit handler,
    // device ID ERDs go to the device ID handler. This ensures responses are
    // processed even when the state is IN_FLIGHT (read already queued).
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed) {
      tiny_erd_t erd = args->read_completed.erd;
      bool feature_bit_active = (this->feature_bit_state_ != FEATURE_BIT_STATE_IDLE &&
                                   this->feature_bit_state_ != FEATURE_BIT_STATE_COMPLETE &&
                                   this->feature_bit_state_ != FEATURE_BIT_STATE_FAILED);
      if (is_feature_bit_erd(erd) && feature_bit_active) {
        this->process_feature_bit_erd_response_(
          erd,
          reinterpret_cast<const uint8_t*>(args->read_completed.data),
          args->read_completed.data_size);
      } else {
        this->process_device_id_erd_response_(
          erd,
          reinterpret_cast<const uint8_t*>(args->read_completed.data),
          args->read_completed.data_size);
      }
    } else if (args->type == tiny_gea3_erd_client_activity_type_read_failed) {
      tiny_erd_t erd = args->read_failed.erd;
      bool feature_bit_active = (this->feature_bit_state_ != FEATURE_BIT_STATE_IDLE &&
                                   this->feature_bit_state_ != FEATURE_BIT_STATE_COMPLETE &&
                                   this->feature_bit_state_ != FEATURE_BIT_STATE_FAILED);
      if (is_feature_bit_erd(erd) && feature_bit_active) {
        ESP_LOGW(TAG, "Failed to read feature bit ERD 0x%04X (reason: %u), advancing to next ERD",
                 erd, args->read_failed.reason);
        this->handle_feature_bit_read_failure_(erd);
      } else {
        ESP_LOGW(TAG, "Failed to read ERD 0x%04X for device ID generation (reason: %u), will retry",
                 erd, args->read_failed.reason);
        this->handle_device_id_read_failure_(erd);
      }
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

  // Apply valid ERD filter if appliance_api_parsing is enabled, the list is ready,
  // and at least one ERD was added to the valid set. An empty set would silently
  // suppress all ERD publishes, so skip the filter if parsing produced no results.
  if (this->appliance_api_parsing_ && this->appliance_api_valid_list_ready_ &&
      !this->appliance_api_valid_erds_.empty()) {
    esphome_mqtt_client_adapter_set_valid_erds_filter(
      &this->mqtt_client_adapter_, &this->appliance_api_valid_erds_);
    ESP_LOGI(TAG, "Appliance API parsing enabled: publishing filtered to %zu valid ERDs",
             this->appliance_api_valid_erds_.size());
  }

  // Initialize MQTT bridge based on mode
  if (use_polling) {
    mqtt_bridge_polling_init(
      &this->mqtt_bridge_polling_,
      &this->timer_group_,
      this->active_erd_client_,
      &this->mqtt_client_adapter_.interface,
      this->polling_interval_ms_,
      this->polling_only_publish_on_change_);
    this->configure_polling_optional_lists_();
  } else {
    mqtt_bridge_init(
      &this->mqtt_bridge_,
      &this->timer_group_,
      this->active_erd_client_,
      &this->mqtt_client_adapter_.interface,
      this->host_address_);
    // Custom ERDs must be polled even in subscribe mode.
    if (!this->custom_erds_vec_.empty()) {
      mqtt_bridge_polling_init(
        &this->custom_erd_bridge_,
        &this->timer_group_,
        this->active_erd_client_,
        &this->mqtt_client_adapter_.interface,
        this->polling_interval_ms_,
        this->polling_only_publish_on_change_);
      this->custom_erd_bridge_.api_parsed_list = this->custom_erds_vec_.data();
      this->custom_erd_bridge_.api_parsed_list_count =
        static_cast<uint16_t>(this->custom_erds_vec_.size());
      this->custom_erd_polling_active_ = true;
      ESP_LOGI(TAG, "Custom ERD polling enabled alongside subscription mode: %zu ERD(s)",
               this->custom_erds_vec_.size());
    }
  }

  this->mqtt_bridge_initialized_ = true;
  ESP_LOGI(TAG, "MQTT bridge initialized successfully");
}

void GeappliancesBridge::configure_polling_optional_lists_() {
  // Set the API-parsed list AFTER init but before any events fire.
  // state_identify_appliance only checks api_parsed_list in signal_read_completed,
  // so setting it here (synchronously, before any events) is safe.
  if (this->appliance_api_parsing_ && this->appliance_api_valid_list_ready_ &&
      !this->appliance_api_valid_erds_vec_.empty()) {
    this->mqtt_bridge_polling_.api_parsed_list = this->appliance_api_valid_erds_vec_.data();
    this->mqtt_bridge_polling_.api_parsed_list_count =
      static_cast<uint16_t>(this->appliance_api_valid_erds_vec_.size());
    ESP_LOGI(TAG, "Polling with API-parsed list of %u ERDs (discovery skipped)",
             this->mqtt_bridge_polling_.api_parsed_list_count);
  }
  // Set custom ERD list AFTER init. Custom ERDs are added to the polling list
  // when state_polling is entered, in addition to the standard or API-parsed list.
  if (!this->custom_erds_vec_.empty()) {
    this->mqtt_bridge_polling_.custom_erd_list = this->custom_erds_vec_.data();
    this->mqtt_bridge_polling_.custom_erd_list_count =
      static_cast<uint16_t>(this->custom_erds_vec_.size());
    ESP_LOGI(TAG, "Polling with %u custom ERD(s)", this->mqtt_bridge_polling_.custom_erd_list_count);
  }
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

    // Destroy the custom ERD polling bridge (if it was active alongside subscription).
    // The full polling bridge initialized below will poll custom ERDs via
    // configure_polling_optional_lists_(), so it does not need to stay active.
    if (this->custom_erd_polling_active_) {
      mqtt_bridge_polling_destroy(&this->custom_erd_bridge_);
      this->custom_erd_polling_active_ = false;
    }

    mqtt_bridge_polling_init(
      &this->mqtt_bridge_polling_,
      &this->timer_group_,
      this->active_erd_client_,
      &this->mqtt_client_adapter_.interface,
      this->polling_interval_ms_,
      this->polling_only_publish_on_change_);
    this->configure_polling_optional_lists_();
    
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
  ESP_LOGCONFIG(TAG, "  Appliance API Parsing: %s", this->appliance_api_parsing_ ? "enabled" : "disabled");
  if (this->appliance_api_valid_list_ready_) {
    ESP_LOGCONFIG(TAG, "  Appliance API Valid ERDs: %zu", this->appliance_api_valid_erds_.size());
  }
  if (!this->custom_erds_vec_.empty()) {
    ESP_LOGCONFIG(TAG, "  Custom ERDs: %zu configured", this->custom_erds_vec_.size());
  }
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after UART (priority 600) and MQTT (priority 50)
  return setup_priority::DATA;  // Priority 600
}

// ---------------------------------------------------------------------------
// Update checker
// ---------------------------------------------------------------------------

void GeappliancesBridge::schedule_update_check_() {
  if (this->update_check_in_progress_) {
    return;
  }

  this->update_check_in_progress_ = true;
  this->update_check_done_ = false;
  this->update_latest_version_buf_[0] = '\0';

  // Spawn a one-shot background task so the HTTP+TLS request does not block
  // the real-time GEA protocol loop.
  BaseType_t rc = xTaskCreate(
      update_check_task_,
      "ge_update_check",
      UPDATE_CHECK_TASK_STACK_SIZE,
      this,
      1,  // low priority – must not starve the main ESPHome loop
      nullptr);

  if (rc != pdPASS) {
    ESP_LOGW(TAG, "Failed to create update-check task (out of memory?)");
    this->update_check_in_progress_ = false;
  }
}

/* static */ void GeappliancesBridge::update_check_task_(void *param) {
  auto *self = reinterpret_cast<GeappliancesBridge *>(param);

  ESP_LOGI(TAG, "Checking for updates (installed: v%s)...",
           GEAPPLIANCES_BRIDGE_VERSION);

  std::string latest;
  bool ok = fetch_latest_release_from_github(latest);

  if (ok && !latest.empty()) {
    strncpy(self->update_latest_version_buf_, latest.c_str(),
            MAX_VERSION_BUF_SIZE - 1);
    self->update_latest_version_buf_[MAX_VERSION_BUF_SIZE - 1] = '\0';
  }
  self->update_check_done_ = true;

  vTaskDelete(nullptr);
}

void GeappliancesBridge::on_update_check_complete_() {
  this->last_update_check_ms_ = millis();

  const std::string installed(GEAPPLIANCES_BRIDGE_VERSION);
  const std::string latest(this->update_latest_version_buf_);

  if (this->installed_version_sensor_ != nullptr) {
    this->installed_version_sensor_->publish_state(installed);
  }

  if (this->latest_version_sensor_ != nullptr && !latest.empty()) {
    this->latest_version_sensor_->publish_state(latest);
    if (latest != installed) {
      ESP_LOGI(TAG, "Update available: v%s -> v%s", installed.c_str(), latest.c_str());
    } else {
      ESP_LOGI(TAG, "Firmware is up to date (v%s)", installed.c_str());
    }
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
