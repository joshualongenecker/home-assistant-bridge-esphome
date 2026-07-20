/*!
 * @file
 * @brief AutodiscoveryManager implementation.
 *
 * Fully self-driving: uses timer-based state machine and subscribes
 * directly to ERD client activity events.  The bridge calls start()
 * to begin discovery; the manager fires on_complete_cb when done.
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
void AutodiscoveryManager::init(tiny_timer_group_t* timer_group,
                                 i_tiny_gea3_erd_client_t* gea3_erd_client,
                                 i_tiny_gea2_erd_client_t* gea2_erd_client,
                                 i_tiny_gea3_erd_client_t* gea2_adapter_client,
                                 i_tiny_gea_interface_t* gea3_interface,
                                 i_tiny_gea_interface_t* gea2_interface,
                                 bool has_gea3_uart,
                                 bool has_gea2_uart,
                                 std::function<void()> on_complete_cb)
{
  this->timer_group_          = timer_group;
  this->gea3_erd_client_      = gea3_erd_client;
  this->gea2_erd_client_      = gea2_erd_client;
  this->gea2_adapter_client_  = gea2_adapter_client;
  this->gea3_interface_       = gea3_interface;
  this->gea2_interface_       = gea2_interface;
  this->has_gea3_uart_        = has_gea3_uart;
  this->has_gea2_uart_        = has_gea2_uart;
  this->on_complete_cb_       = std::move(on_complete_cb);
  this->state_                = AUTODISCOVERY_IDLE;
  this->host_address_         = 0;
  this->active_erd_client_    = nullptr;
  this->gea2_protocol_active_ = false;

  // Subscribe to interface-level on_receive events (primary path).
  // These fire for every valid packet before the ERD client filters by
  // request_id, so we see all broadcast responses even when an
  // unsupported_erd response from one board arrives before the success
  // response from the actual target board.
  if (this->has_gea3_uart_ && this->gea3_interface_ != nullptr) {
    tiny_event_subscription_init(
      &this->gea3_interface_subscription_,
      this,
      AutodiscoveryManager::on_gea3_interface_receive_);
    tiny_event_subscribe(
      tiny_gea_interface_on_receive(this->gea3_interface_),
      &this->gea3_interface_subscription_);
  }

  if (this->has_gea2_uart_ && this->gea2_interface_ != nullptr) {
    tiny_event_subscription_init(
      &this->gea2_interface_subscription_,
      this,
      AutodiscoveryManager::on_gea2_interface_receive_);
    tiny_event_subscribe(
      tiny_gea_interface_on_receive(this->gea2_interface_),
      &this->gea2_interface_subscription_);
  }

  // Subscribe to ERD client activity events as fallback.
  // These still work when there's no broadcast contention (single board).
  if (this->has_gea3_uart_ && this->gea3_erd_client_ != nullptr) {
    tiny_event_subscription_init(
      &this->gea3_activity_subscription_,
      this,
      +[](void* ctx, const void* args) {
        reinterpret_cast<AutodiscoveryManager*>(ctx)->on_gea3_activity_(args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(this->gea3_erd_client_),
      &this->gea3_activity_subscription_);
  }

  if (this->has_gea2_uart_ && this->gea2_adapter_client_ != nullptr) {
    tiny_event_subscription_init(
      &this->gea2_activity_subscription_,
      this,
      +[](void* ctx, const void* args) {
        reinterpret_cast<AutodiscoveryManager*>(ctx)->on_gea2_activity_(args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(this->gea2_adapter_client_),
      &this->gea2_activity_subscription_);
  }
}

void AutodiscoveryManager::cleanup()
{
  // Unsubscribe from interface-level on_receive events.
  if (this->has_gea3_uart_ && this->gea3_interface_ != nullptr) {
    tiny_event_unsubscribe(
      tiny_gea_interface_on_receive(this->gea3_interface_),
      &this->gea3_interface_subscription_);
  }

  if (this->has_gea2_uart_ && this->gea2_interface_ != nullptr) {
    tiny_event_unsubscribe(
      tiny_gea_interface_on_receive(this->gea2_interface_),
      &this->gea2_interface_subscription_);
  }

  // Unsubscribe from GEA3 ERD client activity events.
  if (this->has_gea3_uart_ && this->gea3_erd_client_ != nullptr) {
    tiny_event_unsubscribe(
      tiny_gea3_erd_client_on_activity(this->gea3_erd_client_),
      &this->gea3_activity_subscription_);
  }

  // Unsubscribe from GEA2 adapter ERD client activity events.
  if (this->has_gea2_uart_ && this->gea2_adapter_client_ != nullptr) {
    tiny_event_unsubscribe(
      tiny_gea3_erd_client_on_activity(this->gea2_adapter_client_),
      &this->gea2_activity_subscription_);
  }

  // Stop the broadcast window timer.
  if (this->timer_group_ != nullptr) {
    tiny_timer_stop(this->timer_group_, &this->broadcast_window_timer_);
  }

  // Reset state so a subsequent init() starts fresh.
  this->timer_group_ = nullptr;
  this->gea3_erd_client_ = nullptr;
  this->gea2_erd_client_ = nullptr;
  this->gea2_adapter_client_ = nullptr;
  this->gea3_interface_ = nullptr;
  this->gea2_interface_ = nullptr;
  this->has_gea3_uart_ = false;
  this->has_gea2_uart_ = false;
  this->on_complete_cb_ = std::function<void()>();
  this->state_ = AUTODISCOVERY_IDLE;
  this->host_address_ = 0;
  this->active_erd_client_ = nullptr;
  this->gea2_protocol_active_ = false;
}
void AutodiscoveryManager::start()
{
  if (this->state_ != AUTODISCOVERY_IDLE) {
    return;  // idempotent: already running or complete
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
    // No response -- retry with fallback logic.
    self->schedule_next_broadcast_();
  }
}

void AutodiscoveryManager::on_gea3_activity_(const void* args)
{
  const tiny_gea3_erd_client_on_activity_args_t* a =
    reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);

  // We only care about read_completed during a GEA3 discovery window.
  if (a->type != tiny_gea3_erd_client_activity_type_read_completed) return;
  if (a->read_completed.erd != ERD_APPLIANCE_TYPE)                   return;
  if (a->read_completed.data_size < 1)                              return;
  if (this->active_erd_client_ != nullptr)                          return;  // single response wins

  uint8_t app_type = reinterpret_cast<const uint8_t*>(a->read_completed.data)[0];

  if (this->state_ == AUTODISCOVERY_GEA3_BROADCAST_WAITING) {
    this->on_broadcast_response(a->address, app_type, true);
  }
}

void AutodiscoveryManager::on_gea2_activity_(const void* args)
{
  const tiny_gea3_erd_client_on_activity_args_t* a =
    reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);

  // We only care about read_completed during a GEA2 discovery window.
  if (a->type != tiny_gea3_erd_client_activity_type_read_completed) return;
  if (a->read_completed.erd != ERD_APPLIANCE_TYPE)                   return;
  if (a->read_completed.data_size < 1)                              return;
  if (this->active_erd_client_ != nullptr)                          return;  // single response wins

  uint8_t app_type = reinterpret_cast<const uint8_t*>(a->read_completed.data)[0];

  if (this->state_ == AUTODISCOVERY_GEA2_BROADCAST_WAITING) {
    this->on_broadcast_response(a->address, app_type, false);
  }
}

// =============================================================================
// Interface-level packet receive callbacks (primary discovery path)
//
// These subscribe to the interface on_receive event, which fires for every
// valid packet BEFORE the ERD client filters by request_id. This allows us
// to see all broadcast responses, even when an unsupported_erd response from
// one board arrives before the success response from the actual target board.
// =============================================================================

void AutodiscoveryManager::on_gea3_interface_receive_(void* context, const void* _args)
{
  auto self = reinterpret_cast<AutodiscoveryManager*>(context);
  if (self->active_erd_client_ != nullptr) return;  // already discovered
  if (self->state_ != AUTODISCOVERY_GEA3_BROADCAST_WAITING) return;

  const tiny_gea_interface_on_receive_args_t* args =
    reinterpret_cast<const tiny_gea_interface_on_receive_args_t*>(_args);
  const tiny_gea_packet_t* packet = args->packet;

  // Only interested in read responses (command 0xA1)
  if (packet->payload_length < 3) return;
  if (packet->payload[0] != tiny_gea3_erd_api_command_read_response) return;

  uint8_t result = packet->payload[2];

  // Skip unsupported_erd responses — keep waiting for a success.
  if (result != tiny_gea3_erd_api_read_result_success) return;

  // Success response: verify it's for ERD_APPLIANCE_TYPE with data.
  if (packet->payload_length < 6) return;
  uint16_t erd = (static_cast<uint16_t>(packet->payload[3]) << 8) | packet->payload[4];
  if (erd != ERD_APPLIANCE_TYPE) return;

  uint8_t data_size = packet->payload[5];
  if (data_size < 1) return;

  uint8_t appliance_type = packet->payload[6];
  ESP_LOGD(TAG, "GEA3 board discovered (interface): address=0x%02X appliance_type=%u",
           packet->source, appliance_type);
  self->on_broadcast_response(packet->source, appliance_type, true);
}

void AutodiscoveryManager::on_gea2_interface_receive_(void* context, const void* _args)
{
  auto self = reinterpret_cast<AutodiscoveryManager*>(context);
  if (self->active_erd_client_ != nullptr) return;  // already discovered
  if (self->state_ != AUTODISCOVERY_GEA2_BROADCAST_WAITING) return;

  const tiny_gea_interface_on_receive_args_t* args =
    reinterpret_cast<const tiny_gea_interface_on_receive_args_t*>(_args);
  const tiny_gea_packet_t* packet = args->packet;

  // GEA2 read response: command 0xF0, erd_count, erd_msb, erd_lsb, data_size, data...
  if (packet->payload_length < 5) return;
  if (packet->payload[0] != tiny_gea2_erd_api_command_read_response) return;

  uint8_t erd_count = packet->payload[1];
  if (erd_count != 1) return;

  uint16_t erd = (static_cast<uint16_t>(packet->payload[2]) << 8) | packet->payload[3];
  if (erd != ERD_APPLIANCE_TYPE) return;

  uint8_t data_size = packet->payload[4];
  if (data_size < 1) return;
  if (packet->payload_length < 5 + data_size) return;

  uint8_t appliance_type = packet->payload[5];
  ESP_LOGD(TAG, "GEA2 board discovered (interface): address=0x%02X appliance_type=%u",
           packet->source, appliance_type);
  self->on_broadcast_response(packet->source, appliance_type, false);
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
    ESP_LOGD(TAG, "GEA3 board discovered: address=0x%02X appliance_type=%u",
             address, appliance_type);
    this->host_address_       = address;
    this->active_erd_client_  = this->gea3_erd_client_;
  } else if (!is_gea3 && in_gea2_waiting) {
    ESP_LOGD(TAG, "GEA2 board discovered: address=0x%02X appliance_type=%u",
             address, appliance_type);
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
        // Start the broadcast window timer.
        tiny_timer_start(this->timer_group_,
                         &this->broadcast_window_timer_,
                         AUTODISCOVERY_BROADCAST_WINDOW_MS,
                         this,
                         AutodiscoveryManager::timer_callback_);
        this->state_ = AUTODISCOVERY_GEA3_BROADCAST_WAITING;
      } else {
        ESP_LOGD(TAG, "Broadcast read failed (queue full), retrying next loop iteration");
      }
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
        tiny_timer_start(this->timer_group_,
                         &this->broadcast_window_timer_,
                         AUTODISCOVERY_BROADCAST_WINDOW_MS,
                         this,
                         AutodiscoveryManager::timer_callback_);
        this->state_ = AUTODISCOVERY_GEA2_BROADCAST_WAITING;
      } else {
        ESP_LOGD(TAG, "Broadcast read failed (queue full), retrying next loop iteration");
      }
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
