/*!
 * @file
 * @brief AutodiscoveryManager – appliance autodiscovery state machine.
 *
 * Extracted from GeappliancesBridge as part of the god class refactoring.
 * Encapsulates the GEA3→GEA2 broadcast discovery logic with infinite retry.
 * The manager never gives up — if no board responds, it keeps retrying
 * indefinitely, alternating between GEA3 and GEA2 (if both UARTs are
 * configured).  On completion the manager reports the discovered host
 * address, active ERD client, and protocol type back to the bridge via
 * getters.
 */

#pragma once

#include <cstdint>
#include <functional>

extern "C" {
#include "tiny_gea3_erd_client.h"
#include "tiny_gea2_erd_client.h"
}

namespace esphome {
namespace geappliances_bridge {

static constexpr uint32_t AUTODISCOVERY_STARTUP_DELAY_MS      = 5000;
static constexpr uint32_t AUTODISCOVERY_BROADCAST_WINDOW_MS   = 5000;

enum AutodiscoveryState {
  AUTODISCOVERY_WAITING_5S,
  AUTODISCOVERY_GEA3_BROADCAST_PENDING,
  AUTODISCOVERY_GEA3_BROADCAST_WAITING,
  AUTODISCOVERY_GEA2_BROADCAST_PENDING,
  AUTODISCOVERY_GEA2_BROADCAST_WAITING,
  AUTODISCOVERY_COMPLETE
};

class AutodiscoveryManager {
 public:
  void init(i_tiny_gea3_erd_client_t* gea3_erd_client,
            i_tiny_gea2_erd_client_t* gea2_erd_client,
            i_tiny_gea3_erd_client_t* gea2_adapter_client,
            bool has_gea3_uart,
            bool has_gea2_uart,
            std::function<void()> on_complete_cb);

  void run();

  void on_broadcast_response(uint8_t address, uint8_t appliance_type, bool is_gea3);

  bool is_complete() const { return state_ == AUTODISCOVERY_COMPLETE; }
  bool is_failed()   const { return false; }  // Never fails — retries indefinitely

  uint8_t  get_host_address()       const { return host_address_; }
  i_tiny_gea3_erd_client_t* get_active_erd_client() const { return active_erd_client_; }
  bool     is_gea2_protocol()       const { return gea2_protocol_active_; }
  uint32_t get_retry_count()        const { return retry_count_; }  /* Test-only. */
  AutodiscoveryState get_state()    const { return state_; }

 private:
  i_tiny_gea3_erd_client_t* gea3_erd_client_   = nullptr;
  i_tiny_gea2_erd_client_t* gea2_erd_client_   = nullptr;
  i_tiny_gea3_erd_client_t* gea2_adapter_client_ = nullptr;
  bool has_gea3_uart_ = false;
  bool has_gea2_uart_ = false;
  std::function<void()> on_complete_cb_;

  AutodiscoveryState state_ = AUTODISCOVERY_WAITING_5S;
  uint32_t timer_start_ = 0;
  uint32_t retry_count_ = 0;

  uint8_t  host_address_       = 0;
  i_tiny_gea3_erd_client_t* active_erd_client_ = nullptr;
  bool gea2_protocol_active_ = false;
};

}  // namespace geappliances_bridge
}  // namespace esphome
