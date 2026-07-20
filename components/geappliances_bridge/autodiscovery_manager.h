/*!
 * @file
 * @brief AutodiscoveryManager - appliance autodiscovery state machine.
 *
 * Fully self-driving: owns its own timers and event subscriptions.
 * The bridge calls start() to begin discovery; the manager fires
 * on_complete_cb when a board is found.  Retries indefinitely with
 * GEA3 <-> GEA2 fallback if both UARTs are configured.
 */

// Goal: Locate the connected appliance on the GEA bus by broadcasting to
//       address 0xFF and recording the first responding device's address,
//       protocol type, and active ERD client.
//
// Responsibilities:
//   - Manage the GEA3->GEA2 fallback broadcast discovery sequence
//   - Retry indefinitely until an appliance responds
//   - Own timer-based state machine (no polling from bridge)
//   - Subscribe to interface-level on_receive events to detect broadcast
//     responses (bypassing the ERD client request_id filter, which drops
//     valid responses when an unsupported_erd response arrives first)
//   - Expose the discovered host address and active ERD client via getters
//
// NOT responsible for:
//   - The 5-second startup delay (handled by the startup HSM)
//   - Reading any ERDs beyond ERD_APPLIANCE_TYPE for the broadcast response
//   - Managing MQTT or bridge connections
//   - Any post-discovery work
//
// Dependencies:
//   - i_tiny_gea3_erd_client, i_tiny_gea2_erd_client
//   - i_tiny_gea_interface (for raw packet-level on_receive events)
//   - tiny_timer, tiny_event
// =============================================================================

#pragma once

#include <cstdint>
#include <functional>

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "i_tiny_gea2_erd_client.h"
#include "i_tiny_gea_interface.h"
#include "tiny_event.h"
#include "tiny_event_subscription.h"
#include "tiny_timer.h"
}

namespace esphome {
namespace geappliances_bridge {

static constexpr uint32_t AUTODISCOVERY_BROADCAST_WINDOW_MS = 1000;

enum AutodiscoveryState {
  AUTODISCOVERY_IDLE,
  AUTODISCOVERY_GEA3_BROADCAST_PENDING,
  AUTODISCOVERY_GEA3_BROADCAST_WAITING,
  AUTODISCOVERY_GEA2_BROADCAST_PENDING,
  AUTODISCOVERY_GEA2_BROADCAST_WAITING,
  AUTODISCOVERY_COMPLETE
};

class AutodiscoveryManager {
 public:
  void init(tiny_timer_group_t* timer_group,
            i_tiny_gea3_erd_client_t* gea3_erd_client,
            i_tiny_gea2_erd_client_t* gea2_erd_client,
            i_tiny_gea3_erd_client_t* gea2_adapter_client,
            i_tiny_gea_interface_t* gea3_interface,
            i_tiny_gea_interface_t* gea2_interface,
            bool has_gea3_uart,
            bool has_gea2_uart,
            std::function<void()> on_complete_cb);

  /// Unsubscribe from events and stop timers. Safe to call multiple times.
  void cleanup();

  /// Start the autodiscovery process.  Idempotent if already past IDLE.
  void start();
  uint8_t  get_host_address()       const { return host_address_; }
  i_tiny_gea3_erd_client_t* get_active_erd_client() const { return active_erd_client_; }
  bool     is_gea2_protocol()       const { return gea2_protocol_active_; }
  AutodiscoveryState get_state()    const { return state_; }

 private:
  /// Drive the state machine forward (called from timer callbacks).
  void run();

  /// Called from the ERD client activity subscription callback.
  void on_broadcast_response(uint8_t address, uint8_t appliance_type, bool is_gea3);

  /// Timer callback wrapper (static for tiny_timer API).
  static void timer_callback_(void* context);

  /// Called from the GEA3 ERD client activity subscription callback.
  void on_gea3_activity_(const void* args);

  /// Called from the GEA2 adapter activity subscription callback.
  void on_gea2_activity_(const void* args);

  /// Called from the GEA3 interface on_receive subscription callback.
  static void on_gea3_interface_receive_(void* context, const void* args);

  /// Called from the GEA2 interface on_receive subscription callback.
  static void on_gea2_interface_receive_(void* context, const void* args);

  /// Determine which broadcast to attempt next and transition.
  void schedule_next_broadcast_();

  tiny_timer_group_t* timer_group_ = nullptr;
  i_tiny_gea3_erd_client_t* gea3_erd_client_   = nullptr;
  i_tiny_gea2_erd_client_t* gea2_erd_client_   = nullptr;
  i_tiny_gea3_erd_client_t* gea2_adapter_client_ = nullptr;
  i_tiny_gea_interface_t* gea3_interface_ = nullptr;
  i_tiny_gea_interface_t* gea2_interface_ = nullptr;
  bool has_gea3_uart_ = false;
  bool has_gea2_uart_ = false;
  std::function<void()> on_complete_cb_;

  AutodiscoveryState state_ = AUTODISCOVERY_IDLE;
  tiny_timer_t broadcast_window_timer_;

  uint8_t  host_address_       = 0;
  i_tiny_gea3_erd_client_t* active_erd_client_ = nullptr;
  bool gea2_protocol_active_ = false;

  // Event subscriptions for ERD client activity (kept as fallback)
  tiny_event_subscription_t gea3_activity_subscription_;
  tiny_event_subscription_t gea2_activity_subscription_;
  // Event subscriptions for interface-level packet receive (primary path)
  tiny_event_subscription_t gea3_interface_subscription_;
  tiny_event_subscription_t gea2_interface_subscription_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
