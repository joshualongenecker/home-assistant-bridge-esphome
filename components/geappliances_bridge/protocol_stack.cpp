// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Implement ProtocolStack — the wire-level protocol machinery extracted
//       from GeappliancesBridge (architecture review rec 1). The bridge keeps
//       orchestration (autodiscovery, data-plane bridges, MQTT); this file
//       owns the UART adapters, the GEA2/GEA3 interfaces, both ERD clients,
//       the GEA2→GEA3 adapter, their buffers, the GEA2 tick source, and the
//       shared timer group.
//
// Responsibilities:
//   - init(): initialize the stack for the configured UARTs (zero, one, two)
//   - run(): dispatch one iteration — GEA2 tight loop, GEA3 iteration, or
//     timer-only — with the dual-UART adapter gating
//   - teardown(): reset tick tracking and the GEA2-active latch
//
// NOT responsible for:
//   - Deciding which protocol autodiscovery is using or which data-plane
//     bridges exist (the bridge computes gea2_pending and owns the phases)
//   - Routing ERD client activity to managers (the bridge subscribes)
// =============================================================================

#include "protocol_stack.h"

#include "esphome/core/hal.h"
#include "esphome_time_source.h"
#include "geappliances_bridge_log.h"

namespace esphome {
namespace geappliances_bridge {

GEA_TAG(TAG) = "protocol_stack";

// GEA3 ERD client: ten internal retries.  With request_timeout=250ms and
// request_retries=10, each read gets eleven attempts (2.75s total), which is
// long enough for appliances to service slow first-access NVRAM lookups.
static const tiny_gea3_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};

// GEA2 ERD client: one internal retry.  With request_timeout=250ms and
// request_retries=1, each read gets two attempts (500ms total), giving
// appliances time to service slow first-access NVRAM lookups or transient
// bus collisions without needing bridge-level retry logic.
static const tiny_gea2_erd_client_configuration_t gea2_client_configuration = {
  .request_timeout = 250,
  .request_retries = 1
};

tiny_time_source_ticks_t ProtocolStack::gea2_tick_ticks_(i_tiny_time_source_t* _self)
{
  auto* src = reinterpret_cast<Gea2TickSource*>(_self);
  if (src->stack != nullptr) {
    return src->stack->gea2_tick_count_;
  }
  return 0;
}

void ProtocolStack::init(uint8_t client_address,
                         uart::UARTComponent* gea3_uart,
                         uart::UARTComponent* gea2_uart)
{
  this->gea3_uart_ = gea3_uart;
  this->gea2_uart_ = gea2_uart;

  // Reset tick tracking on re-init (deep sleep wake, ESPHome reconfiguration)
  this->gea2_tick_count_ = 0;
  this->gea2_last_ms_ = 0;
  this->gea2_active_ = false;

  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

  if (this->gea3_uart_ != nullptr) {
    esphome_uart_adapter_init(&this->gea3_uart_adapter_, &this->timer_group_, this->gea3_uart_);

    tiny_gea3_interface_init(
      &this->gea3_interface_,
      &this->gea3_uart_adapter_.interface,
      client_address,
      this->send_queue_buffer_,
      sizeof(this->send_queue_buffer_),
      this->receive_buffer_,
      sizeof(this->receive_buffer_),
      false);

    tiny_gea3_erd_client_init(
      &this->gea3_erd_client_,
      &this->timer_group_,
      &this->gea3_interface_.interface,
      this->client_queue_buffer_,
      sizeof(this->client_queue_buffer_),
      &client_configuration);
  }

  if (this->gea2_uart_ != nullptr) {
    esphome_uart_adapter_init(&this->gea2_uart_adapter_, &this->timer_group_, this->gea2_uart_);

    // Initialize the GEA2 msec-interrupt event that drives the GEA2 interface's
    // internal timeout counters.  The event is published manually inside the
    // GEA2 tight loop (see run()) so it only fires when GEA2 is actually
    // in use — keeping the shared timer_group_ free of a 1 ms periodic timer
    // that would starve GEA3/polling-bridge timers when GEA3 is active.
    tiny_event_init(&this->gea2_msec_interrupt_);
    this->gea2_tick_source_.base.api = &kGea2TickApi;
    this->gea2_tick_source_.stack = this;

    tiny_gea2_interface_init(
      &this->gea2_interface_,
      &this->gea2_uart_adapter_.interface,
      &this->gea2_tick_source_.base,
      &this->gea2_msec_interrupt_.interface,
      client_address,
      this->gea2_send_queue_buffer_,
      sizeof(this->gea2_send_queue_buffer_),
      this->gea2_receive_buffer_,
      sizeof(this->gea2_receive_buffer_),
      false,
      1);

    tiny_gea2_erd_client_init(
      &this->gea2_erd_client_,
      &this->timer_group_,
      &this->gea2_interface_.interface,
      this->gea2_client_queue_buffer_,
      sizeof(this->gea2_client_queue_buffer_),
      &gea2_client_configuration);

    gea2_erd_client_adapter_init(&this->gea2_erd_client_adapter_, &this->gea2_erd_client_.interface);
  }
}

void ProtocolStack::teardown()
{
  // Reset the tick source so the callback reports 0 and re-init starts fresh.
  this->gea2_tick_source_.stack = nullptr;
  this->gea2_tick_count_ = 0;
  this->gea2_last_ms_ = 0;
  this->gea2_active_ = false;
}

void ProtocolStack::run(bool gea2_pending)
{
  // When GEA2 is active (or during GEA2 autodiscovery), run a 100 ms
  // wall-clock busy loop (with a 200 ms hard cap) so the full TX->RX cycle
  // at 19200 baud completes within a single loop() call.
  bool need_gea2_loop = this->gea2_uart_ != nullptr && (gea2_pending || this->gea2_active_);

  // When both UARTs are configured, only enable the adapter for the active
  // protocol.  Both adapters register poll timers in the shared timer_group_,
  // so tiny_timer_group_run() fires both poll callbacks on every call.  By
  // disabling the inactive adapter, its poll() returns early without reading
  // bytes or publishing events to an interface that isn't being driven.
  if (this->gea2_uart_ != nullptr && this->gea3_uart_ != nullptr) {
    esphome_uart_adapter_set_enabled(&this->gea3_uart_adapter_, !need_gea2_loop);
    esphome_uart_adapter_set_enabled(&this->gea2_uart_adapter_, need_gea2_loop);
  }

  if (need_gea2_loop) {
    this->run_tight_loop_([this]() { this->run_gea2_iteration_(); },
                           kGea2LoopDurationMs, kGea2LoopHardCapMs, "GEA2");
  } else if (this->gea3_uart_ != nullptr) {
    this->run_tight_loop_([this]() { this->run_gea3_iteration_(); },
                           kGea3LoopDurationMs, kGea3LoopHardCapMs, "GEA3");
  } else {
    this->run_timer_only_iteration_();
  }
}

void ProtocolStack::run_gea2_iteration_()
{
  if (this->gea2_last_ms_ == 0) {
    this->gea2_last_ms_ = millis();
  }
  uint32_t now_ms = millis();
  uint32_t catchup_count = 0;
  while (this->gea2_last_ms_ < now_ms && catchup_count < kMsecCatchupCap) {
    this->gea2_tick_count_++;
    tiny_event_publish(&this->gea2_msec_interrupt_, nullptr);
    this->gea2_last_ms_++;
    catchup_count++;
  }
  tiny_timer_group_run(&this->timer_group_);
  if (this->gea3_uart_ != nullptr) {
    tiny_timer_group_run(&this->timer_group_);
  }
  tiny_gea2_interface_run(&this->gea2_interface_);
}

void ProtocolStack::run_gea3_iteration_()
{
  tiny_timer_group_run(&this->timer_group_);
  if (this->gea2_uart_ != nullptr) {
    tiny_timer_group_run(&this->timer_group_);
  }
  tiny_gea3_interface_run(&this->gea3_interface_);
}

void ProtocolStack::run_timer_only_iteration_()
{
  tiny_timer_group_run(&this->timer_group_);
  if (this->gea2_uart_ != nullptr) {
    tiny_timer_group_run(&this->timer_group_);
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
