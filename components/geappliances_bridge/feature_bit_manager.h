/*!
 * @file
 * @brief Feature bit manager for the GEA bridge.
 *
 * Parsing is incremental (one step per timer tick) to avoid triggering
 * the ESP32 Task Watchdog Timer.
 */

#ifndef FEATURE_BIT_MANAGER_H
#define FEATURE_BIT_MANAGER_H

#include <cstdint>
#include <string>

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "tiny_event.h"
#include "tiny_event_subscription.h"
#include "tiny_timer.h"
}

namespace esphome {
namespace geappliances_bridge {

enum FeatureBitState {
  FEATURE_BIT_STATE_READING,
  FEATURE_BIT_STATE_PARSING,
  FEATURE_BIT_STATE_FAILED,
  FEATURE_BIT_STATE_COMPLETE,
};

/* Maximum number of ERDs the feature bit manager can track.
 * This covers all possible ERDs from common + appliance feature descriptors. */
#define FEATURE_BIT_MAX_ERDS 645

class FeatureBitManager {
 public:
  /* How many common feature descriptors to process per parse tick.
   * 17 total descriptors; processing 4 per tick spreads the work
   * across ~5 timer callbacks instead of doing all at once, which
   * avoids triggering the Task Watchdog Timer on ESP32-C3. */
  static constexpr uint16_t COMMON_PARSE_PER_CALL = 4;

  static constexpr uint32_t PARSE_TICK_MS = 5;

  static constexpr uint32_t QUEUE_RETRY_MS = 50;

  void init(i_tiny_gea3_erd_client_t* erd_client,
            uint8_t host_address,
            tiny_timer_group_t* timer_group);

  /// Idempotent: safe to call multiple times.
  void cleanup();

  /// Idempotent if already past the first state.
  void start();

  uint16_t get_valid_erd_count() const;
  /// Feature ERDs that were probed but skipped (unsupported or unreadable).
  uint16_t get_skipped_erd_count() const;

  tiny_erd_t get_valid_erd(uint16_t idx) const;

  /// Returns a pointer to the valid ERD array (NULL if none).
  const tiny_erd_t* get_valid_erds() const { return valid_erds_count_ > 0 ? valid_erds_ : nullptr; }

  FeatureBitState get_state() const { return state_; }

  /// True once init() has installed an ERD client; cleanup() resets it.
  /// The bridge's start_feature_bit_reading_() guard, replacing the
  /// bridge-side feature_bit_reading_started_ flag.
  bool is_initialized() const { return this->erd_client_ != nullptr; }

 private:
  void on_erd_activity_(const void* args);

  void handle_read_completed_(tiny_erd_t erd, const void* data, uint8_t size);

  /// Static for the tiny_timer API.
  static void parse_timer_callback_(void* context);

  void parse_next_step_();

  void start_parse_timer_();

  void queue_erd_read_();

  tiny_erd_t get_expected_erd_() const;

  void skip_to_next_erd_(tiny_erd_t failed_erd);

  /// One-shot retry timer callback; static for the tiny_timer API.
  static void queue_retry_timer_callback_(void* context);

  void queue_retry_();

  void add_valid_erd_(tiny_erd_t erd);

  FeatureBitState state_{FEATURE_BIT_STATE_READING};
  uint8_t reading_idx_{0};
  bool read_queued_{false};  /* true while a read is in-flight (guards idempotent start/queue) */

  i_tiny_gea3_erd_client_t* erd_client_{nullptr};
  uint8_t host_address_{0};
  tiny_timer_group_t* timer_group_{nullptr};

  tiny_event_subscription_t erd_activity_subscription_;

  tiny_timer_t parse_timer_;

  tiny_timer_t queue_retry_timer_;

  struct FeatureBitErdData {
    uint8_t data[11][8]{};  /* 11 feature ERDs, each up to 8 bytes */
    uint8_t sizes[11]{};
  };

  FeatureBitErdData erd_data_;
  tiny_erd_t valid_erds_[FEATURE_BIT_MAX_ERDS];
  uint16_t valid_erds_count_{0};
  uint16_t skipped_erds_count_{0};
  bool valid_list_ready_{false};

  uint8_t parse_erd_idx_{0};
  uint16_t common_parse_idx_{0};
  bool parse_common_done_{false};
};

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // FEATURE_BIT_MANAGER_H
