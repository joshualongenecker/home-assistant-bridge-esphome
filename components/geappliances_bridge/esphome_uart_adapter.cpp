#include "esphome_uart_adapter.h"

extern "C" {
#include "tiny_utils.h"
}

static void poll(void* context)
{
  auto self = static_cast<esphome_uart_adapter_t*>(context);

  // Snapshot the available byte count once before processing.
  // This matches the reference implementation (joshualongenecker/
  // home-assistant-adapter-gea3-poll PR#5, esphome_uart_adapter.cpp) and
  // ensures we only consume the bytes that were present when the poll started.
  // Any bytes that arrive *during* event processing — e.g. the half-duplex
  // reflection of a byte just sent by send_next_byte(), or an appliance
  // response that begins arriving while send_ack() is executing — are deferred
  // to the next poll() call.
  // Without this guard, the receive loop can read the ACK-reflection while
  // packet_ready is still true (set by send_ack()), causing the subsequent
  // appliance response STX to be silently dropped in state_idle_cooldown
  // because the !packet_ready guard fails.  This manifests as ERD reads for
  // large payloads (e.g. 32-byte model number) always failing while small
  // payloads (e.g. 1-byte appliance type) succeed by chance.
  int rx_bytes = self->uart->available();

  while (rx_bytes--) {
    uint8_t byte;
    self->uart->read_byte(&byte);

    tiny_uart_on_receive_args_t args = { byte };
    tiny_event_publish(&self->receive_event, &args);
  }

  if (self->sent) {
    self->sent = false;
    tiny_event_publish(&self->send_complete_event, nullptr);
  }
}

static void send(i_tiny_uart_t* _self, uint8_t byte)
{
  auto self = reinterpret_cast<esphome_uart_adapter_t*>(_self);
  self->sent = true;
  self->uart->write_byte(byte);
}

static i_tiny_event_t* on_send_complete(i_tiny_uart_t* _self)
{
  auto self = reinterpret_cast<esphome_uart_adapter_t*>(_self);
  return &self->send_complete_event.interface;
}

static i_tiny_event_t* on_receive(i_tiny_uart_t* _self)
{
  auto self = reinterpret_cast<esphome_uart_adapter_t*>(_self);
  return &self->receive_event.interface;
}

static const i_tiny_uart_api_t api = { send, on_send_complete, on_receive };

extern "C" void esphome_uart_adapter_init(
  esphome_uart_adapter_t* self,
  tiny_timer_group_t* timer_group,
  esphome::uart::UARTComponent* uart)
{
  self->interface.api = &api;
  self->timer_group = timer_group;
  self->uart = uart;
  self->sent = false;

  tiny_event_init(&self->send_complete_event);
  tiny_event_init(&self->receive_event);

  // Poll UART periodically (every ~0ms means as fast as possible in the event loop)
  tiny_timer_start_periodic(timer_group, &self->timer, 0, self, poll);
}
