/*!
 * @file
 * @brief AutodiscoveryManager implementation.
 *
 * Fully self-driving: uses timer-based state machine and subscribes
 * directly to UART byte-level receive events for independent packet
 * assembly.  This bypasses both the GEA interface single-packet buffer
 * and the ERD client request_id filter.  The bridge calls start() to
 * begin discovery; the manager fires on_complete_cb when done.
 */

#include "autodiscovery_manager.h"
#include "geappliances_bridge_constants.h"
#include "tiny_gea3_erd_api.h"
#include "tiny_gea2_erd_api.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

GEA_TAG(TAG) = "autodiscovery";

// =============================================================================
// Public API
// =============================================================================

void AutodiscoveryManager::init(tiny_timer_group_t* timer_group,
                                 i_tiny_gea3_erd_client_t* gea3_erd_client,
                                 i_tiny_gea2_erd_client_t* gea2_erd_client,
                                 i_tiny_gea3_erd_client_t* gea2_adapter_client,
                                 esphome_uart_adapter_t* gea3_uart_adapter,
                                 esphome_uart_adapter_t* gea2_uart_adapter,
                                 bool has_gea3_uart,
                                 bool has_gea2_uart,
                                 uint8_t client_address,
                                 std::function<void()> on_complete_cb)
{
  this->timer_group_          = timer_group;
  this->gea3_erd_client_      = gea3_erd_client;
  this->gea2_erd_client_      = gea2_erd_client;
  this->gea2_adapter_client_  = gea2_adapter_client;
  this->gea3_uart_adapter_    = gea3_uart_adapter;
  this->gea2_uart_adapter_    = gea2_uart_adapter;
  this->has_gea3_uart_        = has_gea3_uart;
  this->has_gea2_uart_        = has_gea2_uart;
  this->client_address_       = client_address;
  this->on_complete_cb_       = std::move(on_complete_cb);
  this->state_                = AUTODISCOVERY_IDLE;
  this->host_address_         = 0;
  this->active_erd_client_    = nullptr;
  this->gea2_protocol_active_ = false;
  this->gea3_rx_              = discover_rx_t{};
  this->gea2_rx_              = discover_rx_t{};

  // Subscribe to UART byte-level receive events.
  // Each byte is fed into our own packet assembler, independent of
  // the GEA interface. This ensures we see ALL packets even when
  // the interface's single-packet buffer drops bytes.
  if (this->has_gea3_uart_ && this->gea3_uart_adapter_ != nullptr) {
    tiny_event_subscription_init(
      &this->gea3_byte_subscription_,
      this,
      AutodiscoveryManager::on_gea3_byte_);
    tiny_event_subscribe(
      &this->gea3_uart_adapter_->receive_event.interface,
      &this->gea3_byte_subscription_);
  }

  if (this->has_gea2_uart_ && this->gea2_uart_adapter_ != nullptr) {
    tiny_event_subscription_init(
      &this->gea2_byte_subscription_,
      this,
      AutodiscoveryManager::on_gea2_byte_);
    tiny_event_subscribe(
      &this->gea2_uart_adapter_->receive_event.interface,
      &this->gea2_byte_subscription_);
  }

}

void AutodiscoveryManager::cleanup()
{
  // Stop the broadcast window timer first to prevent the callback from
  // firing while we unsubscribe and nullify pointers.
  if (this->timer_group_ != nullptr) {
    tiny_timer_stop(this->timer_group_, &this->broadcast_window_timer_);
  }

  // Unsubscribe from UART byte-level receive events.
  if (this->has_gea3_uart_ && this->gea3_uart_adapter_ != nullptr) {
    tiny_event_unsubscribe(
      &this->gea3_uart_adapter_->receive_event.interface,
      &this->gea3_byte_subscription_);
  }

  if (this->has_gea2_uart_ && this->gea2_uart_adapter_ != nullptr) {
    tiny_event_unsubscribe(
      &this->gea2_uart_adapter_->receive_event.interface,
      &this->gea2_byte_subscription_);
  }

  // Reset state so a subsequent init() starts fresh.
  this->timer_group_ = nullptr;
  this->gea3_erd_client_ = nullptr;
  this->gea2_erd_client_ = nullptr;
  this->gea2_adapter_client_ = nullptr;
  this->gea3_uart_adapter_ = nullptr;
  this->gea2_uart_adapter_ = nullptr;
  this->has_gea3_uart_ = false;
  this->has_gea2_uart_ = false;
  this->on_complete_cb_ = std::function<void()>();
  this->state_ = AUTODISCOVERY_IDLE;
  this->host_address_ = 0;
  this->active_erd_client_ = nullptr;
  this->gea2_protocol_active_ = false;
  this->gea3_rx_ = discover_rx_t{};
  this->gea2_rx_ = discover_rx_t{};
}
void AutodiscoveryManager::start()
{
  if (this->state_ != AUTODISCOVERY_IDLE) {
    return;  // idempotent: already running or complete
  }
  if (!this->has_gea3_uart_ && !this->has_gea2_uart_) {
    return;  // nothing to do after cleanup()
  }

  ESP_LOGI(TAG, "Starting autodiscovery");

  // Begin with whichever protocol is available.
  if (this->has_gea3_uart_) {
    this->state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
  } else {
    this->state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
  }
  this->run();
}

// =============================================================================
// Timer callback (static, invoked by tiny_timer_group on broadcast window expiry)
// =============================================================================

void AutodiscoveryManager::timer_callback_(void* context)
{
  AutodiscoveryManager* self = reinterpret_cast<AutodiscoveryManager*>(context);

  // The broadcast window has expired.  Check if we received a response.
  if (self->active_erd_client_ != nullptr) {
    // A board responded during the window -- complete.
    if (self->state_ == AUTODISCOVERY_GEA2_BROADCAST_WAITING) {
      self->gea2_protocol_active_ = true;
    }
    ESP_LOGI(TAG, "%s board discovered at 0x%02X, autodiscovery complete",
             self->gea2_protocol_active_ ? "GEA2" : "GEA3",
             self->host_address_);
    self->state_ = AUTODISCOVERY_COMPLETE;
    if (self->on_complete_cb_) {
      self->on_complete_cb_();
    }
  } else {
    // No response -- reset packet assemblers and retry with fallback logic.
    self->gea3_rx_ = discover_rx_t{};
    self->gea2_rx_ = discover_rx_t{};
    self->schedule_next_broadcast_();
  }
}


// =============================================================================
// UART byte-level receive callbacks
//
// Each byte from the UART is fed into our own packet assembler,
// completely independent of the GEA interface. This ensures we see
// ALL packets even when the interface's single-packet buffer drops
// bytes, or when the ERD client's request_id filter discards valid
// responses.
// =============================================================================

void AutodiscoveryManager::on_gea3_byte_(void* context, const void* args)
{
  auto self = reinterpret_cast<AutodiscoveryManager*>(context);
  const tiny_uart_on_receive_args_t* a =
    reinterpret_cast<const tiny_uart_on_receive_args_t*>(args);
  self->process_byte_(a->byte, true);
}

void AutodiscoveryManager::on_gea2_byte_(void* context, const void* args)
{
  auto self = reinterpret_cast<AutodiscoveryManager*>(context);
  const tiny_uart_on_receive_args_t* a =
    reinterpret_cast<const tiny_uart_on_receive_args_t*>(args);
  self->process_byte_(a->byte, false);
}

void AutodiscoveryManager::process_byte_(uint8_t byte, bool is_gea3)
{
  if (this->active_erd_client_ != nullptr) return;  // already discovered

  discover_rx_t* rx = is_gea3 ? &this->gea3_rx_ : &this->gea2_rx_;
  bool gea3_waiting = (this->state_ == AUTODISCOVERY_GEA3_BROADCAST_WAITING);
  bool gea2_waiting = (this->state_ == AUTODISCOVERY_GEA2_BROADCAST_WAITING);

  if (is_gea3 && !gea3_waiting) return;
  if (!is_gea3 && !gea2_waiting) return;

  // Handle escape sequences.
  // The GEA protocol sends 0xE0 followed by the raw byte for control characters.
  // On receive, the escaped byte is passed through as-is (no transformation).
  if (rx->escaped) {
    rx->escaped = false;
    goto buffer_byte;
  }

  switch (byte) {
    case tiny_gea_esc:
      rx->escaped = true;
      return;

    case tiny_gea_stx:
      rx->count = 0;
      rx->crc = tiny_gea_crc_seed;
      rx->stx_received = true;
      return;

    case tiny_gea_etx: {
      // Guard: ETX without preceding STX is not a valid packet.
      if (!rx->stx_received) {
        return;
      }
      rx->stx_received = false;
      rx->escaped = false;

      // Validate CRC
      if (rx->crc != 0) {
        rx->count = 0;
        return;
      }

      // Minimum: dst(1) + len(1) + src(1) + payload(1) + CRC(2) = 6
      if (rx->count < 6) {
        rx->count = 0;
        return;
      }

      uint8_t destination = rx->buffer[0];
      uint8_t payload_length_on_wire = rx->buffer[1];
      uint8_t source = rx->buffer[2];

      // Guard against payload_length_on_wire underflow.
      if (payload_length_on_wire < tiny_gea_packet_transmission_overhead) {
        rx->count = 0;
        return;
      }
      uint8_t app_payload_len = payload_length_on_wire - tiny_gea_packet_transmission_overhead;

      // Only interested in packets addressed to us or broadcast
      if (destination != this->client_address_ && destination != tiny_gea_broadcast_address) {
        rx->count = 0;
        return;
      }

      if (app_payload_len < 3) {
        rx->count = 0;
        return;
      }

      uint8_t command = rx->buffer[3];

      if (is_gea3) {
        // GEA3: only interested in read responses (0xA1)
        if (command != tiny_gea3_erd_api_command_read_response) {
          rx->count = 0;
          return;
        }

        if (app_payload_len < 6) {
          rx->count = 0;
          return;
        }

        uint8_t result = rx->buffer[5];  // payload[2] = result
        if (result != tiny_gea3_erd_api_read_result_success) {
          // unsupported_erd — skip, keep waiting for a success
          rx->count = 0;
          return;
        }

        uint16_t erd = (static_cast<uint16_t>(rx->buffer[6]) << 8) | rx->buffer[7];
        if (erd != ERD_APPLIANCE_TYPE) {
          rx->count = 0;
          return;
        }

        uint8_t data_size = rx->buffer[8];
        if (data_size < 1 || app_payload_len < 6 + data_size) {
          rx->count = 0;
          return;
        }

        uint8_t appliance_type = rx->buffer[9];
        this->on_broadcast_response(source, appliance_type, true);
      } else {
        // GEA2: read response command is 0xF0, same value as read request per spec.
        if (command != tiny_gea2_erd_api_command_read_response) {
          rx->count = 0;
          return;
        }

        if (app_payload_len < 6) {
          rx->count = 0;
          return;
        }

        uint8_t erd_count = rx->buffer[4];  // payload[1] = erd_count
        if (erd_count != 1) {
          rx->count = 0;
          return;
        }

        uint16_t erd = (static_cast<uint16_t>(rx->buffer[5]) << 8) | rx->buffer[6];
        if (erd != ERD_APPLIANCE_TYPE) {
          rx->count = 0;
          return;
        }

        uint8_t data_size = rx->buffer[7];
        if (data_size < 1 || app_payload_len < 5 + data_size) {
          rx->count = 0;
          return;
        }

        uint8_t appliance_type = rx->buffer[8];
        this->on_broadcast_response(source, appliance_type, false);
      }

      rx->count = 0;
      return;
    }

    default:
buffer_byte:
      if (rx->count < sizeof(rx->buffer)) {
        rx->buffer[rx->count++] = byte;
        rx->crc = tiny_crc16_byte(rx->crc, byte);
      } else {
        ESP_LOGW(TAG, "Discovery packet buffer overflow (count=%u), dropping byte",
                 (unsigned)rx->count);
      }
      return;
  }
}

// =============================================================================
// Internal: handle a broadcast response
// =============================================================================

void AutodiscoveryManager::on_broadcast_response(uint8_t address, uint8_t appliance_type,
                                                  bool is_gea3)
{
  (void)appliance_type;  /* Used only in ESP_LOG calls that may be compiled out. */
  if (this->state_ == AUTODISCOVERY_COMPLETE) return;
  if (this->active_erd_client_ != nullptr)    return;  // single response wins

  bool in_gea3_waiting = (this->state_ == AUTODISCOVERY_GEA3_BROADCAST_WAITING);
  bool in_gea2_waiting = (this->state_ == AUTODISCOVERY_GEA2_BROADCAST_WAITING);

  if (is_gea3 && in_gea3_waiting) {
    this->host_address_       = address;
    this->active_erd_client_  = this->gea3_erd_client_;
  } else if (!is_gea3 && in_gea2_waiting) {
    this->host_address_       = address;
    this->active_erd_client_  = this->gea2_adapter_client_;
  }
}

// =============================================================================
// Internal: schedule the next broadcast after a timeout (retry with fallback)
// =============================================================================

void AutodiscoveryManager::schedule_next_broadcast_()
{
  if (this->has_gea3_uart_ && this->has_gea2_uart_) {
    // Both UARTs: alternate between GEA3 and GEA2.
    if (this->state_ == AUTODISCOVERY_GEA3_BROADCAST_WAITING) {
      ESP_LOGW(TAG, "No GEA3 boards found, trying GEA2...");
      this->state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
    } else {
      ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA3...");
      this->state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
    }
  } else if (this->has_gea3_uart_) {
    ESP_LOGW(TAG, "No GEA3 boards found, retrying GEA3...");
    this->state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
  } else {
    ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA2...");
    this->state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
  }
  this->run();
}

// =============================================================================
// State machine driver (called from start(), schedule_next_broadcast_,
// and indirectly via timer_callback_)
// =============================================================================

void AutodiscoveryManager::run()
{
  switch (this->state_) {
    case AUTODISCOVERY_IDLE:
      // Do nothing -- waiting for start()
      break;

    case AUTODISCOVERY_GEA3_BROADCAST_PENDING: {
      tiny_gea3_erd_client_request_id_t req_id;
      if (tiny_gea3_erd_client_read(this->gea3_erd_client_, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_APPLIANCE_TYPE)) {
        ESP_LOGI(TAG, "Sent GEA3 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_APPLIANCE_TYPE, GEA_BROADCAST_ADDRESS);
      } else {
        ESP_LOGW(TAG, "GEA3 broadcast read failed (queue full), will retry after timeout");
      }
      // Always start the timer and transition to WAITING so the state
      // machine keeps moving even if the broadcast couldn't be queued.
      tiny_timer_start(this->timer_group_,
                       &this->broadcast_window_timer_,
                       AUTODISCOVERY_BROADCAST_WINDOW_MS,
                       this,
                       AutodiscoveryManager::timer_callback_);
      this->state_ = AUTODISCOVERY_GEA3_BROADCAST_WAITING;
      break;
    }

    case AUTODISCOVERY_GEA3_BROADCAST_WAITING:
      // Waiting for the timer to fire (or a response to arrive via subscription).
      // Nothing to do here -- timer_callback_ handles the window expiry.
      break;

    case AUTODISCOVERY_GEA2_BROADCAST_PENDING: {
      tiny_gea2_erd_client_request_id_t req_id;
      if (tiny_gea2_erd_client_read(this->gea2_erd_client_, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_APPLIANCE_TYPE)) {
        ESP_LOGI(TAG, "Sent GEA2 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_APPLIANCE_TYPE, GEA_BROADCAST_ADDRESS);
      } else {
        ESP_LOGW(TAG, "GEA2 broadcast read failed (queue full), will retry after timeout");
      }
      // Always start the timer and transition to WAITING so the state
      // machine keeps moving even if the broadcast couldn't be queued.
      tiny_timer_start(this->timer_group_,
                       &this->broadcast_window_timer_,
                       AUTODISCOVERY_BROADCAST_WINDOW_MS,
                       this,
                       AutodiscoveryManager::timer_callback_);
      this->state_ = AUTODISCOVERY_GEA2_BROADCAST_WAITING;
      break;
    }

    case AUTODISCOVERY_GEA2_BROADCAST_WAITING:
      // Waiting for the timer to fire (or a response to arrive via subscription).
      break;

    case AUTODISCOVERY_COMPLETE:
      // Terminal state -- nothing to do.
      break;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
