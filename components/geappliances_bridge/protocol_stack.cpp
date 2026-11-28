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
//   - init(): initialize the stack for the configured UARTs (zero, one, two);
//     an unconfigured protocol's ring buffers, ERD client and client
//     configuration (and, for GEA2, the GEA2->GEA3 adapter) are compiled out
//     entirely (GEA_BRIDGE_HAS_GEA2 / GEA_BRIDGE_HAS_GEA3, see
//     protocol_stack.h). A single-protocol build never references the other
//     protocol's, which clang rejects under -Werror if they stay declared.
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

#ifdef GEA_BRIDGE_HAS_GEA3
static const tiny_gea3_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};
#endif

#ifdef GEA_BRIDGE_HAS_GEA2
static const tiny_gea2_erd_client_configuration_t gea2_client_configuration = {
  .request_timeout = 250,
  .request_retries = 1
};
#endif

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
#ifdef GEA_BRIDGE_HAS_GEA3
  this->gea3_uart_ = gea3_uart;
#else
  this->gea3_uart_ = nullptr;
  (void) gea3_uart;
#endif
#ifdef GEA_BRIDGE_HAS_GEA2
  this->gea2_uart_ = gea2_uart;
#else
  this->gea2_uart_ = nullptr;
  (void) gea2_uart;
#endif

  this->gea2_tick_count_ = 0;
  this->gea2_last_ms_ = 0;
  this->gea2_active_ = false;

  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

#ifdef GEA_BRIDGE_HAS_GEA3
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
#endif

#ifdef GEA_BRIDGE_HAS_GEA2
  if (this->gea2_uart_ != nullptr) {
    esphome_uart_adapter_init(&this->gea2_uart_adapter_, &this->timer_group_, this->gea2_uart_);

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
#endif
}

void ProtocolStack::teardown()
{
  this->gea2_tick_source_.stack = nullptr;
  this->gea2_tick_count_ = 0;
  this->gea2_last_ms_ = 0;
  this->gea2_active_ = false;
}

void ProtocolStack::run(bool gea2_pending)
{
  bool need_gea2_loop = this->gea2_uart_ != nullptr && (gea2_pending || this->gea2_active_);

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

}
}
