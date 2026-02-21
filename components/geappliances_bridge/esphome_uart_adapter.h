#pragma once

#include "esphome/components/uart/uart.h"

extern "C" {
#include "i_tiny_uart.h"
#include "tiny_timer.h"
}

typedef struct {
  i_tiny_uart_t interface;
  tiny_timer_group_t* timer_group;
  esphome::uart::UARTComponent* uart;
} esphome_uart_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void esphome_uart_adapter_init(
  esphome_uart_adapter_t* self,
  tiny_timer_group_t* timer_group,
  esphome::uart::UARTComponent* uart);

#ifdef __cplusplus
}
#endif
