/*!
 * @file
 * @brief AutodiscoveryManager - appliance autodiscovery state machine.
 *
 * Fully self-driving: owns its own timers and event subscriptions.
 * The bridge calls start() to begin discovery; the manager fires
 * on_complete_cb when a board is found.  Retries indefinitely with
 * GEA3 <-> GEA2 fallback if both UARTs are configured.
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Locate the connected appliance on the GEA bus by broadcasting to
//       address 0xFF and recording the first responding device's address,
//       protocol type, and active ERD client.
//
// Responsibilities:
//   - Manage the GEA3->GEA2 fallback broadcast discovery sequence
//   - Retry indefinitely until an appliance responds
//   - Own timer-based state machine (no polling from bridge)
//   - Subscribe to UART byte-level receive events and do independent
//     packet assembly for broadcast discovery — this bypasses both the
//     GEA interface single-packet buffer (which drops bytes when a packet
//     is already assembled but not yet processed) and the ERD client
//     request_id filter (which drops valid responses when an unsupported_erd
//     response from another board arrives first)
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
//   - esphome_uart_adapter (for byte-level receive events)
//   - tiny_timer, tiny_event
// =============================================================================

#pragma once

#include <cstdint>
#include <functional>

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "i_tiny_gea2_erd_client.h"
#include "tiny_crc16.h"
#include "tiny_gea_constants.h"
#include "tiny_gea_packet.h"
#include "tiny_event.h"
#include "tiny_event_subscription.h"
#include "tiny_timer.h"
}
#include "esphome_uart_adapter.h"

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
            esphome_uart_adapter_t* gea3_uart_adapter,
            esphome_uart_adapter_t* gea2_uart_adapter,
            bool has_gea3_uart,
            bool has_gea2_uart,
            uint8_t client_address,
            std::function<void()> on_complete_cb);

  /// Unsubscribe from events and stop timers. Safe to call multiple times.
  void cleanup();

  /// Start the autodiscovery process.  Idempotent if already past IDLE.
  void start();
  uint8_t  get_host_address()       const { return host_address_; }
  i_tiny_gea3_erd_client_t* get_active_erd_client() const { return active_erd_client_; }
  bool     is_gea2_protocol()       const { return gea2_protocol_active_; }
  AutodiscoveryState get_state()    const { return state_; }

  /// Feed a raw UART byte for discovery packet assembly.
  /// Used by UART receive callbacks and tests.
  void feed_byte(uint8_t byte, bool is_gea3) { process_byte_(byte, is_gea3); }

 private:
  /// Drive the state machine forward (called from timer callbacks).
  void run();

  /// Handle a valid broadcast response from either the UART byte parser.
  void on_broadcast_response(uint8_t address, uint8_t appliance_type, bool is_gea3);

  /// Timer callback wrapper (static for tiny_timer API).
  static void timer_callback_(void* context);


  /// Called from the GEA3 UART adapter byte-level receive subscription.
  static void on_gea3_byte_(void* context, const void* args);

  /// Called from the GEA2 UART adapter byte-level receive subscription.
  static void on_gea2_byte_(void* context, const void* args);

  /// Process a received byte for discovery packet assembly.
  void process_byte_(uint8_t byte, bool is_gea3);

  /// Determine which broadcast to attempt next and transition.
  void schedule_next_broadcast_();

  tiny_timer_group_t* timer_group_ = nullptr;
  i_tiny_gea3_erd_client_t* gea3_erd_client_   = nullptr;
  i_tiny_gea2_erd_client_t* gea2_erd_client_   = nullptr;
  i_tiny_gea3_erd_client_t* gea2_adapter_client_ = nullptr;
  esphome_uart_adapter_t* gea3_uart_adapter_ = nullptr;
  esphome_uart_adapter_t* gea2_uart_adapter_ = nullptr;
  bool has_gea3_uart_ = false;
  bool has_gea2_uart_ = false;
  std::function<void()> on_complete_cb_;

  AutodiscoveryState state_ = AUTODISCOVERY_IDLE;
  tiny_timer_t broadcast_window_timer_;

  uint8_t  host_address_       = 0;
  uint8_t  client_address_     = 0xE4;
  i_tiny_gea3_erd_client_t* active_erd_client_ = nullptr;
  bool gea2_protocol_active_ = false;

  // Per-protocol independent packet assemblers for discovery.
  // Each tracks its own receive state so multiple packets can be
  // assembled concurrently (one per protocol), bypassing the GEA
  // interface single-packet buffer.
  struct discover_rx_t {
    uint16_t crc = 0;
    uint16_t count = 0;
    bool escaped = false;
    bool stx_received = false;
    uint8_t buffer[256];  // max payload + header
  };
  discover_rx_t gea3_rx_{};
  discover_rx_t gea2_rx_{};

  // Event subscriptions for UART byte-level receive
  tiny_event_subscription_t gea3_byte_subscription_;
  tiny_event_subscription_t gea2_byte_subscription_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
