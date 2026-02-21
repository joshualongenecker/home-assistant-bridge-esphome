#include "esphome_uart_adapter.h"
#include "esphome/core/log.h"

extern "C" {
#include "tiny_utils.h"
}

static const char *const TAG = "geappliances_bridge.uart";
static uint32_t rx_byte_count = 0;
static uint32_t tx_byte_count = 0;

static void poll(void* context)
{
  auto self = static_cast<esphome_uart_adapter_t*>(context);

  int bytes_read = 0;
  while (self->uart->available()) {
    uint8_t byte;
    self->uart->read_byte(&byte);
    bytes_read++;
    rx_byte_count++;
    
    tiny_uart_on_receive_args_t args = { byte };
    tiny_event_publish(&self->receive_event, &args);
  }
  
  if (bytes_read > 0) {
    ESP_LOGVV(TAG, "Received %d bytes (total: %u)", bytes_read, rx_byte_count);
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
  tx_byte_count++;
  ESP_LOGVV(TAG, "Sent byte 0x%02X (total: %u)", byte, tx_byte_count);
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
  
  ESP_LOGI(TAG, "UART adapter initialized");
}
