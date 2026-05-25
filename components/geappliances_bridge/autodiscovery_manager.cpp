/*!
 * @file
 * @brief AutodiscoveryManager implementation.
 *
 * Adapted from geappliances_bridge_autodiscovery.cpp as part of the god class
 * refactoring. Logic is identical - only the class context changes.
 */

#include "autodiscovery_manager.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "autodiscovery";

void AutodiscoveryManager::init(i_tiny_gea3_erd_client_t* gea3_erd_client,
                                 i_tiny_gea2_erd_client_t* gea2_erd_client,
                                 i_tiny_gea3_erd_client_t* gea2_adapter_client,
                                 bool has_gea3_uart,
                                 bool has_gea2_uart,
                                 std::function<void()> on_complete_cb)
{
  this->gea3_erd_client_      = gea3_erd_client;
  this->gea2_erd_client_      = gea2_erd_client;
  this->gea2_adapter_client_  = gea2_adapter_client;
  this->has_gea3_uart_        = has_gea3_uart;
  this->has_gea2_uart_        = has_gea2_uart;
  this->on_complete_cb_       = std::move(on_complete_cb);
  this->state_                = AUTODISCOVERY_WAITING_5S;
  this->timer_start_          = esphome::millis();
  this->retry_count_          = 0;
  this->host_address_         = 0;
  this->active_erd_client_    = nullptr;
  this->gea2_protocol_active_ = false;
}

void AutodiscoveryManager::run()
{
  switch (this->state_) {
    case AUTODISCOVERY_WAITING_5S:
      if (esphome::millis() - this->timer_start_ >= AUTODISCOVERY_STARTUP_DELAY_MS) {
        ESP_LOGI(TAG, "5s delay complete, starting autodiscovery");
        if (this->has_gea3_uart_) {
          this->state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
        } else {
          this->state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
        }
      }
      break;

    case AUTODISCOVERY_GEA3_BROADCAST_PENDING: {
      tiny_gea3_erd_client_request_id_t req_id;
      if (tiny_gea3_erd_client_read(this->gea3_erd_client_, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_APPLIANCE_TYPE)) {
        ESP_LOGI(TAG, "Sent GEA3 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_APPLIANCE_TYPE, GEA_BROADCAST_ADDRESS);
        this->timer_start_ = esphome::millis();
        this->state_ = AUTODISCOVERY_GEA3_BROADCAST_WAITING;
      }
      break;
    }

    case AUTODISCOVERY_GEA3_BROADCAST_WAITING:
      if (esphome::millis() - this->timer_start_ >= AUTODISCOVERY_BROADCAST_WINDOW_MS) {
        if (this->active_erd_client_ != nullptr) {
          ESP_LOGI(TAG, "GEA3 board discovered at 0x%02X, autodiscovery complete",
                   this->host_address_);
          this->state_     = AUTODISCOVERY_COMPLETE;
          this->retry_count_ = 0;
          if (this->on_complete_cb_) this->on_complete_cb_();
        } else {
          this->retry_count_++;
          if (this->has_gea2_uart_) {
            ESP_LOGW(TAG, "No GEA3 boards found, trying GEA2... (attempt %u)",
                     this->retry_count_);
            this->state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
          } else {
            ESP_LOGW(TAG, "No GEA3 boards found, retrying GEA3... (attempt %u)",
                     this->retry_count_);
            this->state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
          }
        }
      }
      break;

    case AUTODISCOVERY_GEA2_BROADCAST_PENDING: {
      tiny_gea2_erd_client_request_id_t req_id;
      if (tiny_gea2_erd_client_read(this->gea2_erd_client_, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_APPLIANCE_TYPE)) {
        ESP_LOGI(TAG, "Sent GEA2 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_APPLIANCE_TYPE, GEA_BROADCAST_ADDRESS);
        this->timer_start_ = esphome::millis();
        this->state_ = AUTODISCOVERY_GEA2_BROADCAST_WAITING;
      }
      break;
    }

    case AUTODISCOVERY_GEA2_BROADCAST_WAITING:
      if (esphome::millis() - this->timer_start_ >= AUTODISCOVERY_BROADCAST_WINDOW_MS) {
        if (this->active_erd_client_ != nullptr) {
          ESP_LOGI(TAG, "GEA2 board discovered at 0x%02X, autodiscovery complete",
                   this->host_address_);
          this->state_     = AUTODISCOVERY_COMPLETE;
          this->retry_count_ = 0;
          this->gea2_protocol_active_ = true;
          if (this->on_complete_cb_) this->on_complete_cb_();
        } else {
          this->retry_count_++;
          if (this->has_gea3_uart_) {
            ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA3... (attempt %u)",
                     this->retry_count_);
            this->state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
          } else {
            ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA2... (attempt %u)",
                     this->retry_count_);
            this->state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
          }
        }
      }
      break;

    case AUTODISCOVERY_COMPLETE:
      break;
  }
}

void AutodiscoveryManager::on_broadcast_response(uint8_t address, uint8_t appliance_type,
                                                  bool is_gea3)
{
  (void)appliance_type; /* Used in ESP_LOG calls that may be compiled out. */
  if (this->state_ == AUTODISCOVERY_COMPLETE) return;

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

}  // namespace geappliances_bridge
}  // namespace esphome
