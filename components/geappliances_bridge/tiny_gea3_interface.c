/*!
 * @file
 * @brief
 */

#include <stdbool.h>
#include "tiny_crc16.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea_constants.h"
#include "tiny_utils.h"

typedef tiny_gea3_interface_t self_t;

// Send packet should match tiny_gea_packet_t, but stores data_length (per spec) instead of payload_length
// (used application convenience)
typedef struct {
  uint8_t destination;
  uint8_t data_length;
  uint8_t source;
  uint8_t data[1];
} send_packet_t;

enum {
  send_packet_header_size = offsetof(send_packet_t, data),
  data_length_bytes_not_included_in_data = tiny_gea_packet_transmission_overhead - tiny_gea_packet_overhead,
  crc_size = sizeof(uint16_t),
  packet_bytes_not_included_in_payload = crc_size + offsetof(tiny_gea_packet_t, payload),
  unbuffered_bytes = 2 // STX, ETX
};

enum {
  send_state_data,
  send_state_crc_msb,
  send_state_crc_lsb,
  send_state_etx
};

#define needs_escape(_byte) ((_byte & 0xFC) == tiny_gea_esc)

static bool received_packet_has_valid_crc(self_t* self)
{
  return self->receive_crc == 0;
}

static bool received_packet_has_minimum_valid_length(self_t* self)
{
  return self->receive_count >= packet_bytes_not_included_in_payload;
}

static bool received_packet_has_valid_length(self_t* self)
{
  reinterpret(packet, self->receive_buffer, tiny_gea_packet_t*);
  return (packet->payload_length == self->receive_count + unbuffered_bytes);
}

static bool received_packet_is_addressed_to_me(self_t* self)
{
  reinterpret(packet, self->receive_buffer, tiny_gea_packet_t*);
  return (packet->destination == self->address) ||
    (packet->destination == tiny_gea_broadcast_address) ||
    (self->ignore_destination_address);
}

static void buffer_received_byte(self_t* self, uint8_t byte)
{
  if(self->receive_count == 0) {
    self->receive_crc = tiny_gea_crc_seed;
  }

  if(self->receive_count < self->receive_buffer_size) {
    self->receive_buffer[self->receive_count++] = byte;

    self->receive_crc = tiny_crc16_byte(
      self->receive_crc,
      byte);
  }
}

static void byte_received(void* context, const void* _args)
{
  reinterpret(self, context, self_t*);
  reinterpret(args, _args, const tiny_uart_on_receive_args_t*);
  reinterpret(packet, self->receive_buffer, tiny_gea_packet_t*);
  uint8_t byte = args->byte;

  if(self->receive_packet_ready) {
    return;
  }

  if(self->receive_escaped) {
    self->receive_escaped = false;
    buffer_received_byte(self, byte);
    return;
  }

  switch(byte) {
    case tiny_gea_esc:
      self->receive_escaped = true;
      break;

    case tiny_gea_stx:
      self->receive_count = 0;
      self->stx_received = true;
      break;

    case tiny_gea_etx:
      if(self->stx_received &&
        received_packet_has_minimum_valid_length(self) &&
        received_packet_has_valid_length(self) &&
        received_packet_has_valid_crc(self) &&
        received_packet_is_addressed_to_me(self)) {
        packet->payload_length -= tiny_gea_packet_transmission_overhead;
        self->receive_packet_ready = true;
      }
      self->stx_received = false;
      break;

    default:
      buffer_received_byte(self, byte);
      break;
  }
}

static bool determine_byte_to_send_considering_escapes(self_t* self, uint8_t byte, uint8_t* byteToSend)
{
  if(!self->send_escaped && needs_escape(byte)) {
    self->send_escaped = true;
    *byteToSend = tiny_gea_esc;
  }
  else {
    self->send_escaped = false;
    *byteToSend = byte;
  }

  return !self->send_escaped;
}

static void prepare_buffered_packet_for_transmission(self_t* self)
{
  reinterpret(sendPacket, self->send_buffer, send_packet_t*);
  sendPacket->data_length += tiny_gea_packet_transmission_overhead;
  self->send_crc = tiny_crc16_block(tiny_gea_crc_seed, (const uint8_t*)sendPacket, sendPacket->data_length - data_length_bytes_not_included_in_data);
  self->send_state = send_state_data;
  self->send_offset = 0;
}

static void byte_sent(void* context, const void* args)
{
  reinterpret(self, context, self_t*);
  (void)args;

  if(!self->send_in_progress) {
    if(tiny_queue_count(&self->send_queue) > 0) {
      uint16_t size;
      tiny_queue_dequeue(&self->send_queue, self->send_buffer, &size);
      prepare_buffered_packet_for_transmission(self);
      self->send_in_progress = true;
      tiny_uart_send(self->uart, tiny_gea_stx);
    }
    return;
  }

  uint8_t byteToSend = 0;

  switch(self->send_state) {
    case send_state_data:
      if(determine_byte_to_send_considering_escapes(self, self->send_buffer[self->send_offset], &byteToSend)) {
        reinterpret(sendPacket, self->send_buffer, const send_packet_t*);
        self->send_offset++;

        if(self->send_offset >= sendPacket->data_length - data_length_bytes_not_included_in_data) {
          self->send_state = send_state_crc_msb;
        }
      }
      break;

    case send_state_crc_msb:
      byteToSend = self->send_crc >> 8;
      if(determine_byte_to_send_considering_escapes(self, byteToSend, &byteToSend)) {
        self->send_state = send_state_crc_lsb;
      }
      break;

    case send_state_crc_lsb:
      byteToSend = self->send_crc;
      if(determine_byte_to_send_considering_escapes(self, byteToSend, &byteToSend)) {
        self->send_state = send_state_etx;
      }
      break;

    case send_state_etx:
      self->send_in_progress = false;
      byteToSend = tiny_gea_etx;
      break;
  }

  tiny_uart_send(self->uart, byteToSend);
}

static void populate_send_packet(
  self_t* self,
  tiny_gea_packet_t* packet,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea_interface_send_callback_t callback,
  void* context,
  bool setSourceAddress)
{
  packet->payload_length = payload_length;
  callback(context, (tiny_gea_packet_t*)packet);
  if(setSourceAddress) {
    packet->source = self->address;
  }
  packet->destination = destination;
}

static bool send_worker(
  i_tiny_gea_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea_interface_send_callback_t callback,
  void* context,
  bool setSourceAddress)
{
  reinterpret(self, _self, self_t*);

  if(payload_length + send_packet_header_size > self->send_buffer_size) {
    return false;
  }

  if(self->send_in_progress) {
    uint8_t buffer[255];
    populate_send_packet(self, (tiny_gea_packet_t*)buffer, destination, payload_length, callback, context, setSourceAddress);
    if(!tiny_queue_enqueue(&self->send_queue, buffer, tiny_gea_packet_overhead + payload_length)) {
      return false;
    }
  }
  else {
    reinterpret(sendPacket, self->send_buffer, tiny_gea_packet_t*);
    populate_send_packet(self, sendPacket, destination, payload_length, callback, context, setSourceAddress);
    prepare_buffered_packet_for_transmission(self);
    self->send_in_progress = true;
    tiny_uart_send(self->uart, tiny_gea_stx);
  }

  return true;
}

static bool send(
  i_tiny_gea_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  void* context,
  tiny_gea_interface_send_callback_t callback)
{
  return send_worker(_self, destination, payload_length, callback, context, true);
}

static bool forward(
  i_tiny_gea_interface_t* _self,
  uint8_t destination,
  uint8_t payload_length,
  void* context,
  tiny_gea_interface_send_callback_t callback)
{
  return send_worker(_self, destination, payload_length, callback, context, false);
}

static i_tiny_event_t* on_receive(i_tiny_gea_interface_t* _self)
{
  reinterpret(self, _self, self_t*);
  return &self->on_receive.interface;
}

static const i_tiny_gea_interface_api_t api = { send, forward, on_receive };

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
  bool ignore_destination_address)
{
  self->interface.api = &api;

  self->uart = uart;
  self->address = address;
  self->send_buffer = send_buffer;
  self->send_buffer_size = send_buffer_size;
  self->receive_buffer = receive_buffer;
  self->receive_buffer_size = receive_buffer_size;
  self->ignore_destination_address = ignore_destination_address;
  self->receive_escaped = false;
  self->send_in_progress = false;
  self->send_escaped = false;
  self->stx_received = false;
  self->receive_packet_ready = false;
  self->receive_count = 0;

  tiny_event_init(&self->on_receive);

  tiny_queue_init(&self->send_queue, send_queue_buffer, send_queue_buffer_size);

  tiny_event_subscription_init(&self->byte_received_subscription, self, byte_received);
  tiny_event_subscription_init(&self->byte_sent_subscription, self, byte_sent);

  tiny_event_subscribe(tiny_uart_on_receive(uart), &self->byte_received_subscription);
  tiny_event_subscribe(tiny_uart_on_send_complete(uart), &self->byte_sent_subscription);
}

void tiny_gea3_interface_run(self_t* self)
{
  if(self->receive_packet_ready) {
    tiny_gea_interface_on_receive_args_t args;
    args.packet = (const tiny_gea_packet_t*)self->receive_buffer;
    tiny_event_publish(&self->on_receive, &args);

    // Can only be cleared _after_ publication so that the buffer isn't reused
    self->receive_packet_ready = false;
  }
}
