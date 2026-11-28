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

#define FEATURE_BIT_MAX_ERDS 645

class FeatureBitManager {
 public:
  static constexpr uint16_t COMMON_PARSE_PER_CALL = 4;

  static constexpr uint32_t PARSE_TICK_MS = 5;

  static constexpr uint32_t QUEUE_RETRY_MS = 50;

  void init(i_tiny_gea3_erd_client_t* erd_client,
            uint8_t host_address,
            tiny_timer_group_t* timer_group);

  void cleanup();

  void start();

  uint16_t get_valid_erd_count() const;
  uint16_t get_skipped_erd_count() const;

  tiny_erd_t get_valid_erd(uint16_t idx) const;

  const tiny_erd_t* get_valid_erds() const { return valid_erds_count_ > 0 ? valid_erds_ : nullptr; }

  /// Mutable view of the valid-ERD array, handed to
  /// ErdRegistry::set_valid_erds: the registry sorts it in place and borrows
  /// the pointer instead of copying the 645-entry set (architecture review
  /// rec 9). nullptr for an empty set, which keeps the filter disabled.
  ///
  /// Contract of the borrow (rec 9 review, PR #197):
  /// - ORDER: from this handoff on, the array is in sorted-ERD order, not
  ///   parse order. Consumers of get_valid_erds()/get_valid_erd() must not
  ///   depend on parse order. Verified safe: the only other consumer,
  ///   erd_poll_list_builder, copies the array into its own result and
  ///   qsorts it; get_valid_erd(i) is diagnostics/tests only.
  /// - LIFETIME: the registry points at this array until the bridge is
  ///   destroyed. Both are members of GeappliancesBridge, so the array
  ///   outlives the registry; nothing may free or reallocate it in between
  ///   (it is a fixed member array, never resized).
  tiny_erd_t* get_valid_erds_for_filter() { return valid_erds_count_ > 0 ? valid_erds_ : nullptr; }

  FeatureBitState get_state() const { return state_; }

  bool is_initialized() const { return this->erd_client_ != nullptr; }

 private:
  void on_erd_activity_(const void* args);

  void handle_read_completed_(tiny_erd_t erd, const void* data, uint8_t size);

  static void parse_timer_callback_(void* context);

  void parse_next_step_();

  void start_parse_timer_();

  void queue_erd_read_();

  tiny_erd_t get_expected_erd_() const;

  void skip_to_next_erd_(tiny_erd_t failed_erd);

  static void queue_retry_timer_callback_(void* context);

  void queue_retry_();

  void add_valid_erd_(tiny_erd_t erd);

  FeatureBitState state_{FEATURE_BIT_STATE_READING};
  uint8_t reading_idx_{0};
  bool read_queued_{false};

  i_tiny_gea3_erd_client_t* erd_client_{nullptr};
  uint8_t host_address_{0};
  tiny_timer_group_t* timer_group_{nullptr};

  tiny_event_subscription_t erd_activity_subscription_;

  tiny_timer_t parse_timer_;

  tiny_timer_t queue_retry_timer_;

  struct FeatureBitErdData {
    uint8_t data[11][8]{};
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

}
}

#endif
