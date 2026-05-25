/*!
 * @file
 * @brief FeatureBitManager implementation.
 *
 * Adapted from geappliances_bridge_feature_bits.cpp as part of the god class
 * refactoring. Logic is identical - only the class context changes.
 */

#include "feature_bit_manager.h"
#include "geappliances_bridge_constants.h"
#include "appliance_api_feature_lists.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "feature_bit";

void FeatureBitManager::init(i_tiny_gea3_erd_client_t* erd_client,
                              uint8_t host_address,
                              i_mqtt_client_t* mqtt_client,
                              bool mqtt_initialized)
{
  this->erd_client_       = erd_client;
  this->host_address_     = host_address;
  this->mqtt_client_      = mqtt_client;
  this->mqtt_initialized_ = mqtt_initialized;
  this->state_            = FEATURE_BIT_STATE_READING_0008;
  this->queue_retry_count_ = 0;
  this->parse_pending_    = false;
  this->parse_erd_idx_    = 0;
  this->parse_common_done_ = false;
}

void FeatureBitManager::run()
{
  if (this->erd_client_ == nullptr) {
    return;
  }

  // Deferred parse: called here (not in the GEA callback) so the callback
  // returns quickly and the GEA2 tight-loop continues draining UART bytes.
  // Parsing is incremental — one ERD per loop() call — to avoid triggering
  // ESPHome's 30 ms loop watchdog (the full parse can take 1+ seconds).
  if (this->state_ == FEATURE_BIT_STATE_COMPLETE && this->parse_pending_) {
    this->parse_and_log_feature_bits_();
    // Check if parsing is now fully complete (not just pending).
    if (!this->parse_pending_) {
      /* Parsing is complete — parse_pending_ is already false. */
    }
    return;
  }

  // Map current READING state to the ERD we need to read next.
  tiny_erd_t  feature_erd  = 0;
  const char* feature_name = nullptr;
  (void)feature_name; /* Used in ESP_LOG calls that may be compiled out. */

  switch (this->state_) {
    case FEATURE_BIT_STATE_READING_0008: feature_erd = ERD_APPLIANCE_TYPE;        feature_name = "appliance type (0x0008)";              break;
    case FEATURE_BIT_STATE_READING_0001: feature_erd = ERD_MODEL_NUMBER;          feature_name = "model number (0x0001)";                break;
    case FEATURE_BIT_STATE_READING_0002: feature_erd = ERD_SERIAL_NUMBER;         feature_name = "serial number (0x0002)";               break;
    case FEATURE_BIT_STATE_READING_0092: feature_erd = ERD_COMMON_FEATURE_API;    feature_name = "common feature API (0x0092)";          break;
    case FEATURE_BIT_STATE_READING_0093: feature_erd = ERD_APPLIANCE_FEATURE_API_0; feature_name = "appliance feature API 0 (0x0093)";       break;
    case FEATURE_BIT_STATE_READING_0094: feature_erd = ERD_APPLIANCE_FEATURE_API_1; feature_name = "appliance feature API 1 (0x0094)";       break;
    case FEATURE_BIT_STATE_READING_0095: feature_erd = ERD_APPLIANCE_FEATURE_API_2; feature_name = "appliance feature API 2 (0x0095)";       break;
    case FEATURE_BIT_STATE_READING_0096: feature_erd = ERD_APPLIANCE_FEATURE_API_3; feature_name = "appliance feature API 3 (0x0096)";       break;
    case FEATURE_BIT_STATE_READING_0097: feature_erd = ERD_APPLIANCE_FEATURE_API_4; feature_name = "appliance feature API 4 (0x0097)";       break;
    case FEATURE_BIT_STATE_READING_0109: feature_erd = ERD_APPLIANCE_FEATURE_API_5; feature_name = "appliance feature API 5 (0x0109)";       break;
    case FEATURE_BIT_STATE_READING_010A: feature_erd = ERD_APPLIANCE_FEATURE_API_6; feature_name = "appliance feature API 6 (0x010A)";       break;
    case FEATURE_BIT_STATE_READING_010B: feature_erd = ERD_APPLIANCE_FEATURE_API_7; feature_name = "appliance feature API 7 (0x010B)";       break;
    case FEATURE_BIT_STATE_READING_010C: feature_erd = ERD_APPLIANCE_FEATURE_API_8; feature_name = "appliance feature API 8 (0x010C)";       break;
    case FEATURE_BIT_STATE_READING_010D: feature_erd = ERD_APPLIANCE_FEATURE_API_9; feature_name = "appliance feature API 9 (0x010D)";       break;
    default: return;
  }

  if (tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                  this->host_address_, feature_erd)) {
    ESP_LOGD(TAG, "Queued read for %s", feature_name);
    this->queue_retry_count_  = 0;
    this->state_ = FEATURE_BIT_STATE_IN_FLIGHT;
  } else {
    this->queue_retry_count_++;
    if (this->queue_retry_count_ >= MAX_QUEUE_RETRIES) {
      ESP_LOGI(TAG, "Could not queue read for %s after %u attempts, skipping ERD",
               feature_name, MAX_QUEUE_RETRIES);
      this->queue_retry_count_ = 0;
      this->skip_to_next_feature_erd_(feature_erd);
    } else if (this->queue_retry_count_ % LOG_EVERY_N_RETRIES == 0) {
      ESP_LOGI(TAG, "Failed to queue %s read, retrying... (attempt %u)",
               feature_name, this->queue_retry_count_);
    }
  }
}

void FeatureBitManager::on_erd_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size)
{
  if (data == nullptr) {
    ESP_LOGW(TAG, "Feature bit ERD 0x%04X: null data pointer, skipping", erd);
    return;
  }
  uint8_t copy_size = (size <= 8u) ? size : 8u;
  this->queue_retry_count_ = 0;

  auto queue_next = [this](tiny_erd_t next_erd, FeatureBitState fallback_state) {
    if (this->erd_client_ != nullptr &&
        tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                   this->host_address_, next_erd)) {
      this->state_ = FEATURE_BIT_STATE_IN_FLIGHT;
    } else {
      this->state_ = fallback_state;
    }
  };

  if      (erd == ERD_APPLIANCE_TYPE)      { ESP_LOGD(TAG, "Re-read appliance type (0x0008): %u bytes", copy_size);         queue_next(ERD_MODEL_NUMBER,           FEATURE_BIT_STATE_READING_0001); }
  else if (erd == ERD_MODEL_NUMBER)        { ESP_LOGD(TAG, "Re-read model number (0x0001): %u bytes", copy_size);            queue_next(ERD_SERIAL_NUMBER,          FEATURE_BIT_STATE_READING_0002); }
  else if (erd == ERD_SERIAL_NUMBER)       { ESP_LOGD(TAG, "Re-read serial number (0x0002): %u bytes", copy_size);           queue_next(ERD_COMMON_FEATURE_API,     FEATURE_BIT_STATE_READING_0092); }
  else if (erd == ERD_COMMON_FEATURE_API)      { memcpy(this->erd_data_.erd_0092, data, copy_size); this->erd_data_.erd_0092_size = copy_size; ESP_LOGD(TAG, "Read common feature API (0x0092): %u bytes", copy_size);          queue_next(ERD_APPLIANCE_FEATURE_API_0, FEATURE_BIT_STATE_READING_0093); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_0) { memcpy(this->erd_data_.erd_0093, data, copy_size); this->erd_data_.erd_0093_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 0 (0x0093): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_1, FEATURE_BIT_STATE_READING_0094); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_1) { memcpy(this->erd_data_.erd_0094, data, copy_size); this->erd_data_.erd_0094_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 1 (0x0094): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_2, FEATURE_BIT_STATE_READING_0095); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_2) { memcpy(this->erd_data_.erd_0095, data, copy_size); this->erd_data_.erd_0095_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 2 (0x0095): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_3, FEATURE_BIT_STATE_READING_0096); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_3) { memcpy(this->erd_data_.erd_0096, data, copy_size); this->erd_data_.erd_0096_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 3 (0x0096): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_4, FEATURE_BIT_STATE_READING_0097); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_4) { memcpy(this->erd_data_.erd_0097, data, copy_size); this->erd_data_.erd_0097_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 4 (0x0097): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_5, FEATURE_BIT_STATE_READING_0109); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_5) { memcpy(this->erd_data_.erd_0109, data, copy_size); this->erd_data_.erd_0109_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 5 (0x0109): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_6, FEATURE_BIT_STATE_READING_010A); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_6) { memcpy(this->erd_data_.erd_010A, data, copy_size); this->erd_data_.erd_010A_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 6 (0x010A): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_7, FEATURE_BIT_STATE_READING_010B); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_7) { memcpy(this->erd_data_.erd_010B, data, copy_size); this->erd_data_.erd_010B_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 7 (0x010B): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_8, FEATURE_BIT_STATE_READING_010C); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_8) { memcpy(this->erd_data_.erd_010C, data, copy_size); this->erd_data_.erd_010C_size = copy_size; ESP_LOGD(TAG, "Read appliance feature API 8 (0x010C): %u bytes", copy_size);    queue_next(ERD_APPLIANCE_FEATURE_API_9, FEATURE_BIT_STATE_READING_010D); }
  else if (erd == ERD_APPLIANCE_FEATURE_API_9) {
    memcpy(this->erd_data_.erd_010D, data, copy_size);
    this->erd_data_.erd_010D_size = copy_size;
    ESP_LOGD(TAG, "Read appliance feature API 9 (0x010D): %u bytes", copy_size);
    this->state_       = FEATURE_BIT_STATE_COMPLETE;
    this->parse_pending_ = true;
  }

  // Publish the raw ERD value over MQTT if the adapter is initialized.
  if (this->mqtt_initialized_ && this->mqtt_client_ != nullptr &&
      this->mqtt_client_->api != nullptr && copy_size > 0) {
    this->mqtt_client_->api->update_erd(this->mqtt_client_, erd, data, copy_size);
  }
}

void FeatureBitManager::on_erd_read_failed(tiny_erd_t erd)
{
  ESP_LOGD(TAG, "Feature bit ERD 0x%04X failed or not supported, skipping", erd);
  this->skip_to_next_feature_erd_(erd);
}

void FeatureBitManager::skip_to_next_feature_erd_(tiny_erd_t failed_erd)
{
  switch (failed_erd) {
    case ERD_APPLIANCE_TYPE:          this->state_ = FEATURE_BIT_STATE_READING_0001; break;
    case ERD_MODEL_NUMBER:            this->state_ = FEATURE_BIT_STATE_READING_0002; break;
    case ERD_SERIAL_NUMBER:           this->state_ = FEATURE_BIT_STATE_READING_0092; break;
    case ERD_COMMON_FEATURE_API:      this->state_ = FEATURE_BIT_STATE_READING_0093; break;
    case ERD_APPLIANCE_FEATURE_API_0: this->state_ = FEATURE_BIT_STATE_READING_0094; break;
    case ERD_APPLIANCE_FEATURE_API_1: this->state_ = FEATURE_BIT_STATE_READING_0095; break;
    case ERD_APPLIANCE_FEATURE_API_2: this->state_ = FEATURE_BIT_STATE_READING_0096; break;
    case ERD_APPLIANCE_FEATURE_API_3: this->state_ = FEATURE_BIT_STATE_READING_0097; break;
    case ERD_APPLIANCE_FEATURE_API_4: this->state_ = FEATURE_BIT_STATE_READING_0109; break;
    case ERD_APPLIANCE_FEATURE_API_5: this->state_ = FEATURE_BIT_STATE_READING_010A; break;
    case ERD_APPLIANCE_FEATURE_API_6: this->state_ = FEATURE_BIT_STATE_READING_010B; break;
    case ERD_APPLIANCE_FEATURE_API_7: this->state_ = FEATURE_BIT_STATE_READING_010C; break;
    case ERD_APPLIANCE_FEATURE_API_8: this->state_ = FEATURE_BIT_STATE_READING_010D; break;
    case ERD_APPLIANCE_FEATURE_API_9:
      this->state_       = FEATURE_BIT_STATE_COMPLETE;
      this->parse_pending_ = true;
      break;
    default: break;
  }
}

void FeatureBitManager::parse_and_log_feature_bits_()
{
  // First call: initialize (clear state, parse common features from ERD 0x0092).
  if (this->parse_erd_idx_ == 0 && !this->parse_common_done_) {
    this->valid_erds_.clear();
    this->valid_erds_vec_.clear();
    this->valid_list_ready_ = false;

    // Parse common features from ERD 0x0092.
    if (this->erd_data_.erd_0092_size > 0) {
      uint32_t common_bits = static_cast<uint32_t>(
        read_be64(this->erd_data_.erd_0092, this->erd_data_.erd_0092_size) & 0xFFFFFFFFu);
      ESP_LOGI(TAG, "Common feature API (0x0092) value: 0x%08X", common_bits);
      for (uint16_t i = 0; i < common_feature_descriptor_count; i++) {
        const auto& desc = common_feature_descriptors[i];
        if (common_bits & desc.bit_mask) {
          ESP_LOGI(TAG, "  [SET] Common feature: %s (mask 0x%08X, %u ERDs)",
                   desc.name, desc.bit_mask, desc.erd_count);
          for (uint16_t j = 0; j < desc.erd_count; j++) {
            this->valid_erds_.insert(desc.erds[j]);
          }
        }
      }
    }
    this->parse_common_done_ = true;
    // Fall through to parse the first appliance ERD.
  }

  // Static tables for appliance ERDs (indexed 0-9).
  static const char* const erd_names[10] = {
    "0x0093", "0x0094", "0x0095", "0x0096", "0x0097",
    "0x0109", "0x010A", "0x010B", "0x010C", "0x010D"
  };
  (void)erd_names; /* Used in ESP_LOG calls that may be compiled out. */

  // Process one appliance ERD per call to avoid blocking loop() for too long.
  while (this->parse_erd_idx_ < 10) {
    uint8_t idx = this->parse_erd_idx_;

    // Map index to the actual erd_data_ member.
    const uint8_t* api_bufs[10] = {
      this->erd_data_.erd_0093, this->erd_data_.erd_0094,
      this->erd_data_.erd_0095, this->erd_data_.erd_0096,
      this->erd_data_.erd_0097, this->erd_data_.erd_0109,
      this->erd_data_.erd_010A, this->erd_data_.erd_010B,
      this->erd_data_.erd_010C, this->erd_data_.erd_010D
    };
    const uint8_t api_sizes[10] = {
      this->erd_data_.erd_0093_size, this->erd_data_.erd_0094_size,
      this->erd_data_.erd_0095_size, this->erd_data_.erd_0096_size,
      this->erd_data_.erd_0097_size, this->erd_data_.erd_0109_size,
      this->erd_data_.erd_010A_size, this->erd_data_.erd_010B_size,
      this->erd_data_.erd_010C_size, this->erd_data_.erd_010D_size
    };

    if (api_sizes[idx] < APPLIANCE_FEATURE_ERD_SIZE) {
      this->parse_erd_idx_++;
      continue;
    }
    const uint8_t* api_buf = api_bufs[idx];
    uint16_t appliance_type    = (static_cast<uint16_t>(api_buf[0]) << 8) | api_buf[1];
    uint16_t version           = (static_cast<uint16_t>(api_buf[2]) << 8) | api_buf[3];
    uint32_t feature_bitmap    = (static_cast<uint32_t>(api_buf[4]) << 24) |
                                 (static_cast<uint32_t>(api_buf[5]) << 16) |
                                 (static_cast<uint32_t>(api_buf[6]) <<  8) | api_buf[7];

    if (appliance_type == 0 && version == 0 && feature_bitmap == 0) {
      this->parse_erd_idx_++;
      continue;
    }
    ESP_LOGI(TAG, "Appliance feature ERD %s: type 0x%04X, version %u, features 0x%08X",
             erd_names[idx], appliance_type, version, feature_bitmap);
    bool found_descriptor = false;
    for (uint16_t i = 0; i < appliance_feature_api_descriptor_count; i++) {
      const auto& desc = appliance_feature_api_descriptors[i];
      if (desc.feature_type != appliance_type || desc.version != version) continue;
      found_descriptor = true;
      if (feature_bitmap & desc.bit_mask) {
        ESP_LOGI(TAG, "  [SET] %s (mask 0x%08X, %u ERDs)",
                 desc.name, desc.bit_mask, desc.erd_count);
        for (uint16_t j = 0; j < desc.erd_count; j++) {
          this->valid_erds_.insert(desc.erds[j]);
        }
      }
    }
    if (!found_descriptor) {
      ESP_LOGW(TAG, "  No descriptor for appliance type 0x%04X version %u",
               appliance_type, version);
    }

    // Done with this ERD — advance to next on the following loop() call.
    this->parse_erd_idx_++;
    break;  // Process only one ERD per call.
  }

  // All appliance ERDs parsed — finalize.
  if (this->parse_erd_idx_ >= 10) {
    // Add mandatory ERDs (always published regardless of feature bits).
    static const tiny_erd_t mandatory_erds[] = {
      ERD_MODEL_NUMBER, ERD_SERIAL_NUMBER, ERD_APPLIANCE_TYPE,
      ERD_COMMON_FEATURE_API, ERD_APPLIANCE_FEATURE_API_0,
      ERD_APPLIANCE_FEATURE_API_1, ERD_APPLIANCE_FEATURE_API_2,
      ERD_APPLIANCE_FEATURE_API_3, ERD_APPLIANCE_FEATURE_API_4,
      ERD_APPLIANCE_FEATURE_API_5, ERD_APPLIANCE_FEATURE_API_6,
      ERD_APPLIANCE_FEATURE_API_7, ERD_APPLIANCE_FEATURE_API_8,
      ERD_APPLIANCE_FEATURE_API_9
    };
    for (auto erd : mandatory_erds) {
      this->valid_erds_.insert(erd);
    }

    this->valid_erds_vec_.assign(this->valid_erds_.begin(), this->valid_erds_.end());
    this->valid_list_ready_ = true;
    ESP_LOGI(TAG, "Feature bit parsing complete: %zu valid ERDs", this->valid_erds_.size());
    this->parse_pending_ = false;
  }
}

bool FeatureBitManager::is_complete() const
{
  return this->state_ == FEATURE_BIT_STATE_COMPLETE && !this->parse_pending_;
}

bool FeatureBitManager::is_failed() const
{
  return this->state_ == FEATURE_BIT_STATE_FAILED;
}

bool FeatureBitManager::is_parse_pending() const
{
  return this->parse_pending_;
}

void FeatureBitManager::mark_timed_out()
{
  // Force the manager into a complete state so the startup HSM can
  // transition to bridge_init.  Whatever ERD data has been collected
  // so far is kept; the valid ERD list may be partial or empty,
  // which is fine — the bridge continues without feature filtering.
  this->state_ = FEATURE_BIT_STATE_COMPLETE;
  this->parse_pending_ = true;
  // Immediately drain any pending parsing so is_complete() returns true.
  this->parse_and_log_feature_bits_();
}

const std::set<tiny_erd_t>& FeatureBitManager::get_valid_erds() const
{
  return this->valid_erds_;
}

const std::vector<tiny_erd_t>& FeatureBitManager::get_valid_erds_vec() const
{
  return this->valid_erds_vec_;
}

bool FeatureBitManager::is_valid_list_ready() const
{
  return this->valid_list_ready_;
}

}  // namespace geappliances_bridge
}  // namespace esphome
