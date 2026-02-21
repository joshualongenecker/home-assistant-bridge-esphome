#include "esphome_uart_adapter.h"

extern "C" {
#include "tiny_utils.h"
}

static uint8_t send(i_tiny_uart_t* _self, const void* _buffer, uint16_t buffer_size)
{
  auto self = reinterpret_cast<esphome_uart_adapter_t*>(_self);
  auto buffer = reinterpret_cast<const uint8_t*>(_buffer);
  
  self->uart->write_array(buffer, buffer_size);
  return buffer_size;
}

static i_tiny_event_t* on_receive(i_tiny_uart_t* _self)
{
  auto self = reinterpret_cast<esphome_uart_adapter_t*>(_self);
  return &self->interface.on_receive;
}

static const i_tiny_uart_api_t api = { send, on_receive };

extern "C" void esphome_uart_adapter_init(
  esphome_uart_adapter_t* self,
  tiny_timer_group_t* timer_group,
  esphome::uart::UARTComponent* uart)
{
  self->interface.api = &api;
  self->timer_group = timer_group;
  self->uart = uart;

  tiny_event_init(&self->interface.on_receive);
}
