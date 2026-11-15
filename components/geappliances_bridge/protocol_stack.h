// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Own the wire-level protocol machinery — the UART adapters, the
//       GEA2/GEA3 interfaces, both ERD clients, the GEA2→GEA3 adapter, their
//       buffers, the GEA2 tick source, and the shared timer group — and drive
//       it once per loop() iteration.
//
// Responsibilities:
//   - Initialize the stack for zero, one, or two configured UARTs
//   - Run the GEA2 tight loop / GEA3 iteration / timer-only iteration,
//   - Expose the clients and adapters the orchestration layer wires up
//   - Track the GEA2 millisecond catch-up ticks that drive the GEA2
//     interface's internal timers
//
// NOT responsible for:
//   - Deciding which protocol autodiscovery is using (AutodiscoveryManager) or
//     which data-plane bridges exist (GeappliancesBridge's phase machine)
//   - Routing ERD client activity to managers: the bridge subscribes its own
//     event subscriptions to the exposed clients
//   - MQTT, the ERD cache, or any data-plane component
//
// Dependencies:
//   - esphome_uart_adapter, gea2_erd_client_adapter
//   - tiny_gea2/3 interface + ERD client, tiny_timer, tiny_event
// =============================================================================

#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#ifdef USE_ESP32
#include "esp_task_wdt.h"
#endif

extern "C" {
#include "tiny_timer.h"
#include "tiny_event.h"
#include "i_tiny_time_source.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea3_erd_client.h"
#include "tiny_gea2_interface.h"
#include "tiny_gea2_erd_client.h"
}

#include "esphome_uart_adapter.h"
#include "gea2_erd_client_adapter.h"

namespace esphome {
namespace geappliances_bridge {

class ProtocolStack {
 public:
  // ESPHome applies configuration after the component is constructed, so the
  // UART pointers arrive at init(), not at construction. The stack owns the
  // shared tiny_timer_group_t every data-path component registers timers on;
  // init() pairs it with ESPHome's millis() time source.
  ProtocolStack() = default;

  // Initialize the adapters, interfaces, and ERD clients for the configured
  // UARTs (zero, one, or two). Safe to call again after teardown() (ESPHome
  // re-setup).
  void init(uint8_t client_address,
            uart::UARTComponent* gea3_uart,
            uart::UARTComponent* gea2_uart);

  // Reset per-run state (GEA2 tick tracking, the GEA2-active latch) so a
  // re-init starts fresh.
  void teardown();

  // Drive one iteration: the GEA2 tight loop when GEA2 work is in flight,
  // the GEA3 iteration otherwise, timer-only with no UART.
  //
  // gea2_pending: GEA2 work is in flight this iteration (autodiscovery
  // progress). The autodiscovery manager is orchestration-owned, so the
  // bridge computes this and hands the decision in; the stack ORs in its own
  // GEA2-active latch (set_gea2_active) and gates on whether the GEA2 UART
  // is configured at all.
  void run(bool gea2_pending);

  // Sticky protocol selection: GEA2 is the bridge's active protocol. Set for
  // manual device_id configs with only a GEA2 UART, where autodiscovery
  // produced no active client. Consulted by run() on every iteration.
  void set_gea2_active(bool active) { this->gea2_active_ = active; }
  bool is_gea2_active() const { return this->gea2_active_; }

  bool has_gea3_uart() const { return this->gea3_uart_ != nullptr; }
  bool has_gea2_uart() const { return this->gea2_uart_ != nullptr; }

  i_tiny_gea3_erd_client_t* get_gea3_client() { return &this->gea3_erd_client_.interface; }
  i_tiny_gea2_erd_client_t* get_gea2_client() { return &this->gea2_erd_client_.interface; }
  // The GEA2 ERD client adapter re-publishes GEA2 activity as GEA3-typed
  // events, so subscription-style consumers treat both protocols alike.
  i_tiny_gea3_erd_client_t* get_gea2_adapter_client() { return &this->gea2_erd_client_adapter_.interface; }
  esphome_uart_adapter_t* get_gea3_uart_adapter() { return &this->gea3_uart_adapter_; }
  esphome_uart_adapter_t* get_gea2_uart_adapter() { return &this->gea2_uart_adapter_; }

  // The shared timer group every data-path component (bridges, managers)
  // registers timers on; the stack's iterations run it.
  tiny_timer_group_t* timer_group() { return &this->timer_group_; }

 private:
  void run_gea2_iteration_();
  void run_gea3_iteration_();
  void run_timer_only_iteration_();

  // Tick-counter time source for the GEA2 interface's internal timer group.
  // The counter increments once per real millisecond inside the GEA2 tight
  // loop so the interface's internal timers advance by at most 1 ms per
  // event regardless of the ~50 ms ESPHome framework gap between loop()
  // calls (see docs/spec/protocol_stack.md §3.2 for the full
  // explanation). The source is a member — not a file-scope global — so it
  // resets on re-init (deep sleep wake, ESPHome reconfiguration).
  static tiny_time_source_ticks_t gea2_tick_ticks_(i_tiny_time_source_t* self);

  static constexpr i_tiny_time_source_api_t kGea2TickApi{&ProtocolStack::gea2_tick_ticks_};

  // Run a tight loop for the given iteration function, bounded by a duration
  // and a hard cap. In test builds the loop is replaced with a single call.
  template<typename IterFn>
  void run_tight_loop_(IterFn iter_fn, uint32_t duration_ms,
                       uint32_t hard_cap_ms, const char* protocol_name) {
#ifndef UNIT_TEST_BUILD
    uint32_t loop_start_ms = millis();
    while (millis() - loop_start_ms < duration_ms) {
      if (millis() - loop_start_ms >= hard_cap_ms) {
        ESP_LOGW("protocol_stack", "%s tight loop exceeded hard cap (%u ms), breaking",
                 protocol_name, static_cast<unsigned>(hard_cap_ms));
        break;
      }
#ifdef USE_ESP32
      esp_task_wdt_reset();
#endif
      iter_fn();
    }
#else
    (void)duration_ms;
    (void)hard_cap_ms;
    (void)protocol_name;
    iter_fn();
#endif
  }

  // GEA2 tight-loop duration: covers the full TX→RX cycle at 19200 baud
  // (see docs/spec/protocol_stack.md §3.2 for detailed explanation)
  static constexpr uint32_t kGea2LoopDurationMs = 100;
  // Cap on msec catchup iterations to prevent runaway loops
  static constexpr uint32_t kMsecCatchupCap = 1000;
  static constexpr uint32_t kGea3LoopDurationMs = 10;
  static constexpr uint32_t kGea2LoopHardCapMs = kGea2LoopDurationMs * 2;
  static constexpr uint32_t kGea3LoopHardCapMs = kGea3LoopDurationMs * 2;

  // Tick source handed to the GEA2 interface. stack is null until init()
  // runs the GEA2 branch and again after teardown(), whereupon the callback
  // reports 0.
  struct Gea2TickSource {
    i_tiny_time_source_t base;
    ProtocolStack* stack;
  };

  tiny_timer_group_t timer_group_{};
  uart::UARTComponent* gea3_uart_;
  uart::UARTComponent* gea2_uart_;

  esphome_uart_adapter_t gea3_uart_adapter_{};
  tiny_gea3_interface_t gea3_interface_{};
  // GEA3 receive buffer — one complete on-wire packet.
  // Max payload is tiny_gea_packet_max_payload_length (248) + 7 bytes overhead = 255.
  uint8_t receive_buffer_[255];
  // GEA3 send queue — holds up to ~4 pending outbound packets (255 bytes each).
  uint8_t send_queue_buffer_[1000];

  tiny_gea3_erd_client_t gea3_erd_client_{};
  /* GEA3 client queue buffer — sized to hold enough read requests for the
   * custom-ERD polling bridge (up to ~31 reads × 6 bytes each ≈ 188 bytes)
   * plus in-flight subscription acknowledgments and write requests.
   * Increased from 1024 to 2048 to prevent ring-buffer overflow when the
   * polling bridge and subscription bridge share the same ERD client;
   * overflow corrupts adjacent heap metadata causing
   * prvCheckTasksWaitingTermination crashes (see erd_bridge_poll.cpp). */
  uint8_t client_queue_buffer_[4096];

  esphome_uart_adapter_t gea2_uart_adapter_{};
  tiny_gea2_interface_t gea2_interface_{};

  // GEA2 receive buffer — one complete on-wire packet.
  // Max payload is tiny_gea_packet_max_payload_length (248) + 7 bytes overhead = 255.
  uint8_t gea2_receive_buffer_[255];
  // GEA2 send queue — larger than GEA3 to absorb more packets at 19200 baud
  // where the slower bus means the tight loop processes fewer packets per call.
  uint8_t gea2_send_queue_buffer_[4096];

  tiny_gea2_erd_client_t gea2_erd_client_{};
  uint8_t gea2_client_queue_buffer_[4096];

  gea2_erd_client_adapter_t gea2_erd_client_adapter_{};

  // Event fired once per millisecond to drive the GEA2 interface's internal
  // timers. Published manually inside the GEA2 tight loop (not via a periodic
  // timer in the shared timer_group_) so the 1 ms interrupt never fires in
  // the GEA3 single-pass path and cannot starve the GEA3/polling-bridge
  // timers in the shared timer group.
  tiny_event_t gea2_msec_interrupt_{};

  Gea2TickSource gea2_tick_source_{};
  tiny_time_source_ticks_t gea2_tick_count_{0};
  uint32_t gea2_last_ms_{0};

  // Sticky "GEA2 is the active protocol" latch (see set_gea2_active).
  bool gea2_active_{false};
};

}  // namespace geappliances_bridge
}  // namespace esphome
