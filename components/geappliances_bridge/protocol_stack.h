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

#include "esphome/core/defines.h"
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

// -----------------------------------------------------------------------------
// Build-variant gates (architecture review rec 9)
//
// The ring buffers below are the largest static cost of the wire-level stack:
// 5351 B for GEA3 (255 + 1000 + 4096) and 8447 B for GEA2 (255 + 4096 + 4096).
// A device that configures only one UART cannot use the other protocol's
// buffers at all, so __init__.py emits GEA_BRIDGE_HAS_GEA3 /
// GEA_BRIDGE_HAS_GEA2 exactly when the matching UART is configured, and the
// buffers for the unconfigured protocol are not declared.
//
// A build with NEITHER define compiles both stacks. That is the host test
// build (Makefile), which runs no ESPHome codegen: the suite keeps exercising
// both paths without carrying variant-specific defines.
//
// Load-bearing coupling: the defines are emitted under exactly the condition
// that makes the UART pointer non-null at runtime — set_gea3_uart /
// set_gea2_uart are only called for a configured UART — and init() nulls the
// pointer when the matching define is absent, so has_gea2_uart() /
// has_gea3_uart() can never report a protocol whose buffers are compiled out.
// Gated with the buffers: each protocol's ERD client, and the GEA2->GEA3
// adapter. Nothing outside the matching #ifdef may touch them: the accessors
// return nullptr in a build without the protocol, and init() only reaches them
// under the gate. Declaring them anyway left them unreferenced in a
// single-protocol build, which clang rejects under -Werror
// (-Wunused-private-field). The interface, UART adapter and tick source
// members stay declared because run() and teardown() reference them without
// a gate.
// -----------------------------------------------------------------------------
#if !defined(GEA_BRIDGE_HAS_GEA2) && !defined(GEA_BRIDGE_HAS_GEA3)
#define GEA_BRIDGE_HAS_GEA2 1
#define GEA_BRIDGE_HAS_GEA3 1
#endif

namespace esphome {
namespace geappliances_bridge {

class ProtocolStack {
 public:
  ProtocolStack() = default;

  void init(uint8_t client_address,
            uart::UARTComponent* gea3_uart,
            uart::UARTComponent* gea2_uart);

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

  void set_gea2_active(bool active) { this->gea2_active_ = active; }
  bool is_gea2_active() const { return this->gea2_active_; }

  bool has_gea3_uart() const { return this->gea3_uart_ != nullptr; }
  bool has_gea2_uart() const { return this->gea2_uart_ != nullptr; }

  i_tiny_gea3_erd_client_t* get_gea3_client() {
#ifdef GEA_BRIDGE_HAS_GEA3
    return &this->gea3_erd_client_.interface;
#else
    return nullptr;
#endif
  }
  i_tiny_gea2_erd_client_t* get_gea2_client() {
#ifdef GEA_BRIDGE_HAS_GEA2
    return &this->gea2_erd_client_.interface;
#else
    return nullptr;
#endif
  }
  i_tiny_gea3_erd_client_t* get_gea2_adapter_client() {
#ifdef GEA_BRIDGE_HAS_GEA2
    return &this->gea2_erd_client_adapter_.interface;
#else
    return nullptr;
#endif
  }
  esphome_uart_adapter_t* get_gea3_uart_adapter() {
#ifdef GEA_BRIDGE_HAS_GEA3
    return &this->gea3_uart_adapter_;
#else
    return nullptr;
#endif
  }
  esphome_uart_adapter_t* get_gea2_uart_adapter() {
#ifdef GEA_BRIDGE_HAS_GEA2
    return &this->gea2_uart_adapter_;
#else
    return nullptr;
#endif
  }

  tiny_timer_group_t* timer_group() { return &this->timer_group_; }

 private:
  void run_gea2_iteration_();
  void run_gea3_iteration_();
  void run_timer_only_iteration_();

  static tiny_time_source_ticks_t gea2_tick_ticks_(i_tiny_time_source_t* self);

  static constexpr i_tiny_time_source_api_t kGea2TickApi{&ProtocolStack::gea2_tick_ticks_};

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

  static constexpr uint32_t kGea2LoopDurationMs = 100;
  static constexpr uint32_t kMsecCatchupCap = 1000;
  static constexpr uint32_t kGea3LoopDurationMs = 10;
  static constexpr uint32_t kGea2LoopHardCapMs = kGea2LoopDurationMs * 2;
  static constexpr uint32_t kGea3LoopHardCapMs = kGea3LoopDurationMs * 2;

  struct Gea2TickSource {
    i_tiny_time_source_t base;
    ProtocolStack* stack;
  };

  tiny_timer_group_t timer_group_{};
  uart::UARTComponent* gea3_uart_;
  uart::UARTComponent* gea2_uart_;

  esphome_uart_adapter_t gea3_uart_adapter_{};
  tiny_gea3_interface_t gea3_interface_{};
#ifdef GEA_BRIDGE_HAS_GEA3
  tiny_gea3_erd_client_t gea3_erd_client_{};
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[1000];

  uint8_t client_queue_buffer_[4096];
#endif

  esphome_uart_adapter_t gea2_uart_adapter_{};
  tiny_gea2_interface_t gea2_interface_{};

#ifdef GEA_BRIDGE_HAS_GEA2
  tiny_gea2_erd_client_t gea2_erd_client_{};
  gea2_erd_client_adapter_t gea2_erd_client_adapter_{};
  uint8_t gea2_receive_buffer_[255];
  uint8_t gea2_send_queue_buffer_[4096];

  uint8_t gea2_client_queue_buffer_[4096];
#endif

  tiny_event_t gea2_msec_interrupt_{};

  Gea2TickSource gea2_tick_source_{};
  tiny_time_source_ticks_t gea2_tick_count_{0};
  uint32_t gea2_last_ms_{0};

  bool gea2_active_{false};
};

}
}
