/*!
 * @file
 * @brief
 *
 * This component is interrupt-aware and handles byte transmit/receive in the
 * interrupt context. Publication of messages is done via a background task in
 * tiny_gea3_interface_run() so the application does not have to do
 * anything special.
 *
 * This component queues sent packets into the provided send queue buffer. If a
 * packet is being sent when another send is requested, the packet will be placed
 * into the queue provided there is sufficient space.
 */

#ifndef tiny_gea3_interface_h
#define tiny_gea3_interface_h

#include <stdint.h>
#include "hal/i_tiny_uart.h"
#include "i_tiny_gea_interface.h"
#include "tiny_event.h"
#include "tiny_queue.h"

typedef struct {
  i_tiny_gea_interface_t interface;

  tiny_event_t on_receive;
  tiny_event_subscription_t byte_received_subscription;
  tiny_event_subscription_t byte_sent_subscription;
  i_tiny_uart_t* uart;
  uint8_t* send_buffer;
  uint8_t* receive_buffer;

  tiny_queue_t send_queue;

  uint16_t send_crc;
  uint16_t receive_crc;

  uint8_t address;

  uint8_t send_buffer_size;
  uint8_t send_offset;
  volatile bool send_in_progress;

  uint8_t receive_buffer_size;
  uint8_t receive_count;
  volatile bool receive_packet_ready; // Set by ISR, cleared by background

  uint8_t send_state;
  bool send_escaped;
  bool receive_escaped;
  bool stx_received;

  bool ignore_destination_address;
} tiny_gea3_interface_t;

/*!
 * Initialize a GEA3 interface.
 */
void tiny_gea3_interface_init(
  tiny_gea3_interface_t* self,
  i_tiny_uart_t* uart,
  uint8_t address,
  uint8_t* send_buffer,
  uint8_t send_buffer_size,
  uint8_t* receive_buffer,
  uint8_t receive_buffer_size,
  uint8_t* send_queue_buffer,
  size_t send_queue_buffer_size,
  bool ignore_destination_address);

/*!
 * Run the interface and publish received packets.
 */
void tiny_gea3_interface_run(
  tiny_gea3_interface_t* self);

#endif
