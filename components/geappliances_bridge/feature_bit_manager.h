/*!
 * @file
 * @brief Feature bit manager for the GEA bridge.
 *
 * Handles reading and parsing of appliance API feature bit ERDs
 * (0x0092-0x0097, 0x0109-0x010D), building a filtered ERD list
 * for polling mode and gating HA discovery.
 */

#ifndef FEATURE_BIT_MANAGER_H
#define FEATURE_BIT_MANAGER_H

#include <cstdint>
#include <set>
#include <vector>
#include <string>

extern "C" {
#include "tiny_gea3_erd_client.h"
}

#include "i_mqtt_client.h"

namespace esphome {
namespace geappliances_bridge {

enum FeatureBitState {
  FEATURE_BIT_STATE_IDLE,
  FEATURE_BIT_STATE_READING_0008,
  FEATURE_BIT_STATE_READING_0001,
  FEATURE_BIT_STATE_READING_0002,
  FEATURE_BIT_STATE_READING_0092,
  FEATURE_BIT_STATE_READING_0093,
  FEATURE_BIT_STATE_READING_0094,
  FEATURE_BIT_STATE_READING_0095,
  FEATURE_BIT_STATE_READING_0096,
  FEATURE_BIT_STATE_READING_0097,
  FEATURE_BIT_STATE_READING_0109,
  FEATURE_BIT_STATE_READING_010A,
  FEATURE_BIT_STATE_READING_010B,
  FEATURE_BIT_STATE_READING_010C,
  FEATURE_BIT_STATE_READING_010D,
  FEATURE_BIT_STATE_IN_FLIGHT,
  FEATURE_BIT_STATE_COMPLETE,
  FEATURE_BIT_STATE_FAILED,
};

/*!
 * Manages reading and parsing of appliance API feature bit ERDs.
 */
class FeatureBitManager {
 public:
  static constexpr uint32_t MAX_QUEUE_RETRIES = 1000;
  static constexpr uint32_t LOG_EVERY_N_RETRIES = 50;

  void init(i_tiny_gea3_erd_client_t* erd_client,
            uint8_t host_address,
            i_mqtt_client_t* mqtt_client,
            bool mqtt_initialized);

  void run();

  void on_erd_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size);
  void on_erd_read_failed(tiny_erd_t erd);

  bool is_complete() const;
  bool is_failed() const;
  bool is_parse_pending() const;

  // Mark the manager as complete (with whatever data has been collected so far).
  // Used when the phase times out — the bridge should continue startup rather
  // than hang indefinitely waiting for ERD reads that will never arrive.
  void mark_timed_out();

  const std::set<tiny_erd_t>& get_valid_erds() const;
  const std::vector<tiny_erd_t>& get_valid_erds_vec() const;
  bool is_valid_list_ready() const;

  struct FeatureBitErdData {
    uint8_t erd_0092[8]{};
    uint8_t erd_0093[8]{};
    uint8_t erd_0094[8]{};
    uint8_t erd_0095[8]{};
    uint8_t erd_0096[8]{};
    uint8_t erd_0097[8]{};
    uint8_t erd_0109[8]{};
    uint8_t erd_010A[8]{};
    uint8_t erd_010B[8]{};
    uint8_t erd_010C[8]{};
    uint8_t erd_010D[8]{};
    uint8_t erd_0092_size{0};
    uint8_t erd_0093_size{0};
    uint8_t erd_0094_size{0};
    uint8_t erd_0095_size{0};
    uint8_t erd_0096_size{0};
    uint8_t erd_0097_size{0};
    uint8_t erd_0109_size{0};
    uint8_t erd_010A_size{0};
    uint8_t erd_010B_size{0};
    uint8_t erd_010C_size{0};
    uint8_t erd_010D_size{0};
  };

  const FeatureBitErdData& get_erd_data() const { return erd_data_; }

  FeatureBitState get_state() const { return state_; }
  uint32_t get_queue_retry_count() const { return queue_retry_count_; }  /* Test-only. */

 private:
  void skip_to_next_feature_erd_(tiny_erd_t failed_erd);
  void parse_and_log_feature_bits_();

  FeatureBitState state_{FEATURE_BIT_STATE_IDLE};
  uint32_t queue_retry_count_{0};
  bool parse_pending_{false};
  uint8_t parse_erd_idx_{0};  // which appliance ERD we're parsing next (0-9)
  bool parse_common_done_{false};  // whether ERD 0x0092 common features are parsed

  FeatureBitErdData erd_data_;

  std::set<tiny_erd_t> valid_erds_;
  std::vector<tiny_erd_t> valid_erds_vec_;
  bool valid_list_ready_{false};

  i_tiny_gea3_erd_client_t* erd_client_{nullptr};
  uint8_t host_address_{0};
  i_mqtt_client_t* mqtt_client_{nullptr};
  bool mqtt_initialized_{false};

  tiny_gea3_erd_client_request_id_t pending_request_id_{0};
};

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // FEATURE_BIT_MANAGER_H
