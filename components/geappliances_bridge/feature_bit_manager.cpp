/*!
 * @file
 * @brief FeatureBitManager implementation.
 *
 * Fully self-driving: subscribes to ERD client activity events and
 * uses a periodic timer for incremental parsing.
 */

#include "feature_bit_manager.h"
#include "geappliances_bridge_constants.h"
#include "appliance_api_feature_lists.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

GEA_TAG(TAG) = "feature_bit";

/* Ordered list of feature bit ERDs to read. */
static constexpr tiny_erd_t feature_erd_list[] = {
  ERD_COMMON_FEATURE_API,
  ERD_APPLIANCE_FEATURE_API_0, ERD_APPLIANCE_FEATURE_API_1,
  ERD_APPLIANCE_FEATURE_API_2, ERD_APPLIANCE_FEATURE_API_3,
  ERD_APPLIANCE_FEATURE_API_4, ERD_APPLIANCE_FEATURE_API_5,
  ERD_APPLIANCE_FEATURE_API_6, ERD_APPLIANCE_FEATURE_API_7,
  ERD_APPLIANCE_FEATURE_API_8, ERD_APPLIANCE_FEATURE_API_9,
};
static constexpr uint8_t FEATURE_ERD_COUNT = sizeof(feature_erd_list) / sizeof(feature_erd_list[0]);

// =============================================================================
// Public API
// =============================================================================

void FeatureBitManager::init(i_tiny_gea3_erd_client_t* erd_client,
                              uint8_t host_address,
                              tiny_timer_group_t* timer_group)
{
  /* Unsubscribe from any previous init() to avoid dangling subscriptions on re-init. */
  if (this->erd_client_) {
    tiny_event_unsubscribe(
      tiny_gea3_erd_client_on_activity(this->erd_client_),
      &this->erd_activity_subscription_);
  }
  if (this->timer_group_) {
    tiny_timer_stop(this->timer_group_, &this->parse_timer_);
    tiny_timer_stop(this->timer_group_, &this->queue_retry_timer_);
  }

  if (erd_client == nullptr) {
    ESP_LOGE(TAG, "init() called with null erd_client");
    return;
  }
  if (timer_group == nullptr) {
    ESP_LOGE(TAG, "init() called with null timer_group");
    return;
  }
  this->erd_client_    = erd_client;
  this->host_address_  = host_address;
  this->timer_group_   = timer_group;
  this->state_         = FEATURE_BIT_STATE_READING;
  this->reading_idx_   = 0;
  this->parse_erd_idx_ = 0;
  this->common_parse_idx_ = 0;
  this->parse_common_done_ = false;
  this->valid_list_ready_ = false;
  this->valid_erds_count_ = 0;

  /* Reset ERD data buffers (struct has default initializers, so just re-default-construct) */
  this->erd_data_ = FeatureBitErdData{};

  /* Subscribe to ERD client activity events so we can drive the read sequence
   * without the bridge polling us. */
  tiny_event_subscription_init(
    &this->erd_activity_subscription_,
    this,
    +[](void* ctx, const void* args) {
      reinterpret_cast<FeatureBitManager*>(ctx)->on_erd_activity_(args);
    });
  tiny_event_subscribe(
    tiny_gea3_erd_client_on_activity(this->erd_client_),
    &this->erd_activity_subscription_);
}

void FeatureBitManager::cleanup()
{
  /* Unsubscribe from ERD client activity events. */
  if (this->erd_client_) {
    tiny_event_unsubscribe(
      tiny_gea3_erd_client_on_activity(this->erd_client_),
      &this->erd_activity_subscription_);
  }

  /* Stop any active timers. */
  if (this->timer_group_) {
    tiny_timer_stop(this->timer_group_, &this->parse_timer_);
    tiny_timer_stop(this->timer_group_, &this->queue_retry_timer_);
  }

  /* Reset state so a subsequent init() starts fresh. */
  this->erd_client_ = nullptr;
  this->timer_group_ = nullptr;
  this->host_address_ = 0;
  this->state_ = FEATURE_BIT_STATE_READING;
  this->reading_idx_ = 0;
}

void FeatureBitManager::start()
{
  /* Defensive: don't dereference null erd_client_ */
  if (this->erd_client_ == nullptr) {
    return;
  }
  /* Idempotent: only queue the first read if we're at the start and haven't queued yet. */
  if (this->state_ != FEATURE_BIT_STATE_READING || this->reading_idx_ != 0 || this->read_queued_) {
    return;
  }
  ESP_LOGI(TAG, "Reading appliance API feature bits...");
  this->queue_erd_read_();
}

uint16_t FeatureBitManager::get_valid_erd_count() const
{
  return this->valid_erds_count_;
}

tiny_erd_t FeatureBitManager::get_valid_erd(uint16_t idx) const
{
  if (idx >= this->valid_erds_count_) {
    return 0;
  }
  return this->valid_erds_[idx];
}

// =============================================================================
// ERD client activity event handler
// =============================================================================

void FeatureBitManager::on_erd_activity_(const void* args)
{
  const tiny_gea3_erd_client_on_activity_args_t* a =
    reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);

  /* Ignore events for other appliances (e.g., GEA2 adapter responses) */
  if (a->address != this->host_address_) {
    return;
  }

  /* Ignore events once we're done reading (PARSING, COMPLETE, or FAILED).
   * The parse timer handles the PARSING phase independently. */
  if (this->state_ == FEATURE_BIT_STATE_PARSING || this->state_ == FEATURE_BIT_STATE_COMPLETE ||
      this->state_ == FEATURE_BIT_STATE_FAILED) {
    return;
  }

  /* Determine which ERD we're currently waiting for. */
  tiny_erd_t expected_erd = this->get_expected_erd_();

  /* If we haven't queued a read yet (queue was full), retry now. */
  if (!this->read_queued_) {
    this->queue_erd_read_();
    return;
  }

  /* Only process events for the ERD we're actually waiting for.
   * This prevents unrelated reads (e.g., from the polling bridge) from
   * clearing read_queued_ and corrupting the read sequence. */
  if (a->type == tiny_gea3_erd_client_activity_type_read_completed) {
    if (a->read_completed.erd == expected_erd) {
      this->handle_read_completed_(a->read_completed.erd,
                                    a->read_completed.data,
                                    a->read_completed.data_size);
    }
  } else if (a->type == tiny_gea3_erd_client_activity_type_read_failed) {
    if (a->read_failed.erd == expected_erd) {
      this->skip_to_next_erd_(a->read_failed.erd);
    }
  }
}

// =============================================================================
// Handle a successful ERD read
// =============================================================================

void FeatureBitManager::handle_read_completed_(tiny_erd_t erd, const void* data, uint8_t size)
{
  this->read_queued_ = false;

  /* Validate the incoming ERD matches the expected one in the sequence.
   * If ERDs arrive out of order or an unexpected ERD fires, skip it to
   * avoid storing data in the wrong slot. */
  uint8_t idx = this->reading_idx_;
  if (idx >= FEATURE_ERD_COUNT || erd != feature_erd_list[idx]) {
    ESP_LOGE(TAG, "Feature bit ERD 0x%04X: unexpected (expected 0x%04X at index %u), skipping",
             erd, (idx < FEATURE_ERD_COUNT) ? feature_erd_list[idx] : 0, idx);
    this->skip_to_next_erd_(erd);
    return;
  }

  if (data == nullptr) {
    ESP_LOGW(TAG, "Feature bit ERD 0x%04X: null data pointer, skipping", erd);
    this->skip_to_next_erd_(erd);
    return;
  }

  uint8_t copy_size = (size <= 8u) ? size : 8u;
  if (copy_size < size) {
    ESP_LOGW(TAG, "Feature bit ERD 0x%04X: data truncated from %u to %u bytes",
             erd, size, copy_size);
  }

  /* Store the ERD data in the corresponding buffer. */
  memcpy(this->erd_data_.data[idx], data, copy_size);
  this->erd_data_.sizes[idx] = copy_size;

  ESP_LOGD(TAG, "Read feature ERD 0x%04X (%u/%u): %u bytes",
           erd, idx + 1, FEATURE_ERD_COUNT, copy_size);

  /* Advance to next ERD or transition to parsing. */
  this->reading_idx_++;
  if (this->reading_idx_ >= FEATURE_ERD_COUNT) {
    this->state_ = FEATURE_BIT_STATE_PARSING;
    this->start_parse_timer_();
    return;
  }

  /* Queue the next ERD in the sequence. */
  this->queue_erd_read_();
}

// =============================================================================
// Queue the next ERD read based on current state
// =============================================================================

void FeatureBitManager::queue_erd_read_()
{
  if (this->reading_idx_ >= FEATURE_ERD_COUNT) {
    return;  /* Not in a READING state - nothing to queue */
  }

  tiny_erd_t feature_erd = feature_erd_list[this->reading_idx_];

  /* Try to queue the read. If the queue is full, stay in the current state
   * and schedule a retry timer. */
  tiny_gea3_erd_client_request_id_t req_id;
  if (tiny_gea3_erd_client_read(this->erd_client_, &req_id, this->host_address_, feature_erd)) {
    ESP_LOGV(TAG, "Queued read for feature ERD 0x%04X", feature_erd);
    this->read_queued_ = true;
  } else {
    /* Queue is full — arm a one-shot retry timer so we don't stall
     * indefinitely when no other ERD activity occurs. */
    ESP_LOGD(TAG, "Queue full for ERD 0x%04X, scheduling retry in %u ms",
             feature_erd, static_cast<unsigned>(QUEUE_RETRY_MS));
    tiny_timer_start(this->timer_group_,
                     &this->queue_retry_timer_,
                     QUEUE_RETRY_MS,
                     this,
                     FeatureBitManager::queue_retry_timer_callback_);
  }
}
tiny_erd_t FeatureBitManager::get_expected_erd_() const
{
  if (this->reading_idx_ >= FEATURE_ERD_COUNT) {
    return 0;  /* Not in a READING state */
  }
  return feature_erd_list[this->reading_idx_];
}

// =============================================================================
// Queue retry timer callback
// =============================================================================

void FeatureBitManager::queue_retry_timer_callback_(void* context)
{
  reinterpret_cast<FeatureBitManager*>(context)->queue_retry_();
}

void FeatureBitManager::queue_retry_()
{
  /* Don't retry if we've moved past the READING state (e.g., an event
   * arrived and completed the read before the timer fired). */
  if (this->state_ == FEATURE_BIT_STATE_PARSING || this->state_ == FEATURE_BIT_STATE_COMPLETE ||
      this->state_ == FEATURE_BIT_STATE_FAILED) {
    return;
  }
  if (this->read_queued_) {
    /* Read was already queued by an intervening event — nothing to do. */
    return;
  }
  this->queue_erd_read_();
}

// =============================================================================
// Skip to the next ERD in the sequence (on failure)
// =============================================================================

void FeatureBitManager::skip_to_next_erd_(tiny_erd_t failed_erd)
{
  this->read_queued_ = false;

  ESP_LOGD(TAG, "Feature bit ERD 0x%04X failed or not supported, skipping", failed_erd);

  /* ERD 0x0092 (common feature API) is the foundation for all feature
   * filtering. Without it, we have no way to know which ERDs are
   * supported. Mark as failed so the bridge falls back to full polling. */
  if (failed_erd == ERD_COMMON_FEATURE_API) {
    ESP_LOGW(TAG, "Common feature API (0x0092) not supported; feature bit filtering disabled");
    this->state_ = FEATURE_BIT_STATE_FAILED;
    return;
  }

  /* Advance to next ERD or transition to parsing. */
  this->reading_idx_++;
  if (this->reading_idx_ >= FEATURE_ERD_COUNT) {
    this->state_ = FEATURE_BIT_STATE_PARSING;
    this->start_parse_timer_();
    return;
  }

  /* Queue the next ERD read. */
  this->queue_erd_read_();
}

// =============================================================================
// Timer-driven incremental parsing
// =============================================================================

void FeatureBitManager::start_parse_timer_()
{
  /* Start periodic timer for incremental parsing */
  tiny_timer_start_periodic(this->timer_group_,
                            &this->parse_timer_,
                            PARSE_TICK_MS,
                            this,
                            FeatureBitManager::parse_timer_callback_);
}

void FeatureBitManager::parse_timer_callback_(void* context)
{
  reinterpret_cast<FeatureBitManager*>(context)->parse_next_step_();
}

void FeatureBitManager::add_valid_erd_(tiny_erd_t erd)
{
  /* Deduplicate: check if already present. */
  for (uint16_t i = 0; i < this->valid_erds_count_; i++) {
    if (this->valid_erds_[i] == erd) return;
  }
  if (this->valid_erds_count_ >= FEATURE_BIT_MAX_ERDS) return;
  this->valid_erds_[this->valid_erds_count_++] = erd;
}

void FeatureBitManager::parse_next_step_()
{
  /* First call: initialize (clear state). */
  if (this->parse_erd_idx_ == 0 && !this->parse_common_done_ && this->common_parse_idx_ == 0) {
    this->valid_erds_count_ = 0;
    this->valid_list_ready_ = false;

    /* Index 0 is the common feature API (0x0092). */
    if (this->erd_data_.sizes[0] > 0) {
      uint32_t common_bits = static_cast<uint32_t>(
        read_be64(this->erd_data_.data[0], this->erd_data_.sizes[0]) & 0xFFFFFFFFu);
      ESP_LOGI(TAG, "Common feature API (0x0092) value: 0x%08lX", (unsigned long)common_bits);
      (void)common_bits; /* Used in ESP_LOGI that may be compiled out. */
    }
  }

  /* Parse common features incrementally (up to COMMON_PARSE_PER_CALL descriptors per call). */
  if (!this->parse_common_done_) {
    uint16_t start = this->common_parse_idx_;
    uint16_t end = (start + COMMON_PARSE_PER_CALL > common_feature_descriptor_count)
                   ? common_feature_descriptor_count : start + COMMON_PARSE_PER_CALL;

    if (this->erd_data_.sizes[0] > 0) {
      uint32_t common_bits = static_cast<uint32_t>(
        read_be64(this->erd_data_.data[0], this->erd_data_.sizes[0]) & 0xFFFFFFFFu);
      for (uint16_t i = start; i < end; i++) {
        const auto& desc = common_feature_descriptors[i];
        if (common_bits & desc.bit_mask) {
          ESP_LOGI(TAG, "  [SET] Common feature: %s (mask 0x%08lX, %u ERDs)",
                   desc.name, (unsigned long)desc.bit_mask, desc.erd_count);
          for (uint16_t j = 0; j < desc.erd_count; j++) {
            this->add_valid_erd_(desc.erds[j]);
          }
        }
      }
    }

    this->common_parse_idx_ = end;
    if (this->common_parse_idx_ >= common_feature_descriptor_count) {
      this->parse_common_done_ = true;
      /* Fall through to parse the first appliance ERD on the next tick. */
      return;
    }
    /* Not done with common features yet - return to keep timer tick short. */
    return;
  }

  /* Static tables for appliance ERDs (indexed 0-9 in parse_erd_idx_,
   * mapping to erd_data_ indices 1-10). */
  [[maybe_unused]] static const char* const erd_names[10] = {
    "0x0093", "0x0094", "0x0095", "0x0096", "0x0097",
    "0x0109", "0x010A", "0x010B", "0x010C", "0x010D"
  };

  /* Process one appliance ERD per tick to avoid blocking the timer for too long. */
  if (this->parse_erd_idx_ < 10) {
    uint8_t idx = this->parse_erd_idx_;
    uint8_t data_idx = idx + 1;  /* erd_data_ index: 1-10 for appliance features */

    if (this->erd_data_.sizes[data_idx] < APPLIANCE_FEATURE_ERD_SIZE) {
      this->parse_erd_idx_++;
      return;  /* Will process next ERD on next tick */
    }
    const uint8_t* api_buf = this->erd_data_.data[data_idx];
    uint16_t appliance_type    = (static_cast<uint16_t>(api_buf[0]) << 8) | api_buf[1];
    uint16_t version           = (static_cast<uint16_t>(api_buf[2]) << 8) | api_buf[3];
    uint32_t feature_bitmap    = (static_cast<uint32_t>(api_buf[4]) << 24) |
                                 (static_cast<uint32_t>(api_buf[5]) << 16) |
                                 (static_cast<uint32_t>(api_buf[6]) <<  8) | api_buf[7];

    if (appliance_type == 0 && version == 0 && feature_bitmap == 0) {
      this->parse_erd_idx_++;
      return;  /* Will process next ERD on next tick */
    }
    ESP_LOGI(TAG, "Appliance feature ERD %s: type 0x%04X, version %u, features 0x%08lX",
             erd_names[idx], appliance_type, version, (unsigned long)feature_bitmap);
    bool found_descriptor = false;
    for (uint16_t i = 0; i < appliance_feature_api_descriptor_count; i++) {
      const auto& desc = appliance_feature_api_descriptors[i];
      if (desc.feature_type != appliance_type || desc.version != version) continue;
      found_descriptor = true;
      if (feature_bitmap & desc.bit_mask) {
        ESP_LOGI(TAG, "  [SET] %s (mask 0x%08lX, %u ERDs)",
                 desc.name, (unsigned long)desc.bit_mask, desc.erd_count);
        for (uint16_t j = 0; j < desc.erd_count; j++) {
          this->add_valid_erd_(desc.erds[j]);
        }
      }
    }
    if (!found_descriptor) {
      ESP_LOGW(TAG, "  No descriptor for appliance type 0x%04X version %u",
               appliance_type, version);
    }

    this->parse_erd_idx_++;
    return;  /* Process only one ERD per tick. */
  }

  /* All appliance ERDs parsed - finalize.
   * Add mandatory ERDs (always published regardless of feature bits). */
  static const tiny_erd_t mandatory_erds[] = {
    ERD_MODEL_NUMBER, ERD_SERIAL_NUMBER, ERD_APPLIANCE_TYPE,
    ERD_COMMON_FEATURE_API, ERD_APPLIANCE_FEATURE_API_0,
    ERD_APPLIANCE_FEATURE_API_1, ERD_APPLIANCE_FEATURE_API_2,
    ERD_APPLIANCE_FEATURE_API_3, ERD_APPLIANCE_FEATURE_API_4,
    ERD_APPLIANCE_FEATURE_API_5, ERD_APPLIANCE_FEATURE_API_6,
    ERD_APPLIANCE_FEATURE_API_7, ERD_APPLIANCE_FEATURE_API_8,
    ERD_APPLIANCE_FEATURE_API_9
  };
  for (uint16_t i = 0; i < sizeof(mandatory_erds) / sizeof(mandatory_erds[0]); i++) {
    this->add_valid_erd_(mandatory_erds[i]);
  }

  this->valid_list_ready_ = true;
  ESP_LOGI(TAG, "Feature bit parsing complete: %u valid ERDs", this->valid_erds_count_);

  /* Stop the parse timer and transition to COMPLETE */
  tiny_timer_stop(this->timer_group_, &this->parse_timer_);
  this->state_ = FEATURE_BIT_STATE_COMPLETE;
}

}  // namespace geappliances_bridge
}  // namespace esphome
