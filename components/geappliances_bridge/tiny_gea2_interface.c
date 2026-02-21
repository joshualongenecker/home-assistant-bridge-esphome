/*!
 * @file
 * @brief
 */

#include <stdbool.h>
#include "tiny_crc16.h"
#include "tiny_fsm.h"
#include "tiny_gea2_interface.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea_constants.h"
#include "tiny_gea_packet.h"
#include "tiny_utils.h"

enum {
  gea2_reflection_timeout_msec = 6,
  tiny_gea_ack_timeout_msec = 8,
  gea2_broadcast_mask = 0xF0,
  default_retries = 2,
  gea2_interbyte_timeout_msec = 6,
};

typedef tiny_gea2_interface_t self_t;

// send packet should match tiny_gea_packet_t, but stores data_length (per spec) instead of payload_length
// (used application convenience)
typedef struct {
  uint8_t destination;
  uint8_t data_length;
  uint8_t source;
  uint8_t data[1];
} send_packet_t;

enum {
  signal_byte_received = tiny_fsm_signal_user_start,
  signal_interbyte_timeout,
  signal_send_ready,
  signal_idle_cooldown_timeout,
  signal_reflection_timeout,
  signal_collision_idle_timeout,
  signal_ack_timeout
};

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
  send_state_etx,
  send_state_stx,
  send_state_done,
};

static void state_idle(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_receive(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_idle_cooldown(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_send(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_wait_for_ack(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);
static void state_collision_cooldown(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data);

static self_t* interface_from_fsm(tiny_fsm_t* fsm)
{
  return container_of(self_t, _private.fsm, fsm);
}

static void byte_received(void* context, const void* _args)
{
  self_t* instance = context;
  const tiny_uart_on_receive_args_t* args = _args;

  tiny_fsm_send_signal(&instance->_private.fsm, signal_byte_received, &args->byte);
}

#define needs_escape(_byte) ((_byte & 0xFC) == tiny_gea_esc)

#define is_broadcast_address(_address) ((gea2_broadcast_mask & _address) == gea2_broadcast_mask)

static void state_idle(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* instance = interface_from_fsm(fsm);
  switch(signal) {
    case tiny_fsm_signal_entry:
    case signal_send_ready:
      if(instance->_private.send.active) {
        tiny_fsm_transition(fsm, state_send);
      }
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;

      if(instance->_private.receive.packet_ready) {
        break;
      }

      if(*byte == tiny_gea_stx && !instance->_private.receive.packet_ready) {
        tiny_fsm_transition(fsm, state_receive);
      }
      else {
        tiny_fsm_transition(fsm, state_idle_cooldown);
      }
    } break;
  }
}

static void reflection_timeout(void* context)
{
  self_t* instance = context;
  tiny_fsm_send_signal(&instance->_private.fsm, signal_reflection_timeout, NULL);
}

static bool determine_byte_to_send_considering_escapes(self_t* instance, uint8_t byte, uint8_t* byte_to_send)
{
  if(!instance->_private.send.escaped && needs_escape(byte)) {
    instance->_private.send.escaped = true;
    *byte_to_send = tiny_gea_esc;
  }
  else {
    instance->_private.send.escaped = false;
    *byte_to_send = byte;
  }

  return !instance->_private.send.escaped;
}

static void send_next_byte(self_t* instance)
{
  uint8_t byte_to_send = 0;

  tiny_timer_start(
    &instance->_private.timer_group,
    &instance->_private.timer,
    gea2_reflection_timeout_msec,
    instance,
    reflection_timeout);

  switch(instance->_private.send.state) {
    case send_state_stx:
      byte_to_send = tiny_gea_stx;
      instance->_private.send.state = send_state_data;
      break;

    case send_state_data:
      if(determine_byte_to_send_considering_escapes(instance, instance->_private.send.buffer[instance->_private.send.offset], &byte_to_send)) {
        reinterpret(send_packet, instance->_private.send.buffer, const send_packet_t*);
        instance->_private.send.offset++;

        if(instance->_private.send.offset >= send_packet->data_length - data_length_bytes_not_included_in_data) {
          instance->_private.send.state = send_state_crc_msb;
        }
      }
      break;

    case send_state_crc_msb:
      byte_to_send = instance->_private.send.crc >> 8;
      if(determine_byte_to_send_considering_escapes(instance, byte_to_send, &byte_to_send)) {
        instance->_private.send.state = send_state_crc_lsb;
      }
      break;

    case send_state_crc_lsb:
      byte_to_send = instance->_private.send.crc;
      if(determine_byte_to_send_considering_escapes(instance, byte_to_send, &byte_to_send)) {
        instance->_private.send.state = send_state_etx;
      }
      break;

    case send_state_etx:
      byte_to_send = tiny_gea_etx;
      instance->_private.send.state = send_state_done;
      break;
  }

  instance->_private.send.expected_reflection = byte_to_send;
  tiny_uart_send(instance->_private.uart, byte_to_send);
}

static void handle_send_failure(self_t* instance)
{
  if(instance->_private.send.retries > 0) {
    instance->_private.send.retries--;
  }
  else {
    instance->_private.send.active = false;
  }

  tiny_fsm_transition(&instance->_private.fsm, state_collision_cooldown);
}

static void state_send(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* instance = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      instance->_private.send.state = send_state_stx;
      instance->_private.send.offset = 0;
      instance->_private.send.escaped = false;

      send_next_byte(instance);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      if(*byte == instance->_private.send.expected_reflection) {
        if(instance->_private.send.state == send_state_done) {
          reinterpret(send_packet, instance->_private.send.buffer, send_packet_t*);

          if(is_broadcast_address(send_packet->destination)) {
            instance->_private.send.active = false;
            tiny_fsm_transition(fsm, state_idle_cooldown);
          }
          else {
            tiny_fsm_transition(fsm, state_wait_for_ack);
          }
        }
        else {
          send_next_byte(instance);
        }
      }
      else {
        handle_send_failure(instance);
      }
    } break;

    case signal_reflection_timeout:
      handle_send_failure(instance);
      tiny_fsm_transition(fsm, state_idle_cooldown);
      break;
  }
}

static void handle_success(self_t* instance)
{
  instance->_private.send.active = false;
  tiny_fsm_transition(&instance->_private.fsm, state_idle_cooldown);
}

static void ack_timeout(void* context)
{
  self_t* instance = context;
  tiny_fsm_send_signal(&instance->_private.fsm, signal_ack_timeout, NULL);
}

static void start_ack_timeout_timer(self_t* instance)
{
  tiny_timer_start(
    &instance->_private.timer_group,
    &instance->_private.timer,
    tiny_gea_ack_timeout_msec,
    instance,
    ack_timeout);
}

static void state_wait_for_ack(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* instance = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      start_ack_timeout_timer(instance);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      if(*byte == tiny_gea_ack) {
        handle_success(instance);
      }
      else {
        handle_send_failure(instance);
      }
    } break;

    case signal_ack_timeout:
      handle_send_failure(instance);
      break;
  }
}

static bool received_packet_has_valid_crc(self_t* self)
{
  return self->_private.receive.crc == 0;
}

static bool received_packet_has_minimum_valid_length(self_t* self)
{
  return self->_private.receive.count >= packet_bytes_not_included_in_payload;
}

static bool received_packet_has_valid_length(self_t* self)
{
  reinterpret(packet, self->_private.receive.buffer, tiny_gea_packet_t*);
  return (packet->payload_length == self->_private.receive.count + unbuffered_bytes);
}

static void buffer_received_byte(self_t* instance, uint8_t byte)
{
  if(instance->_private.receive.count == 0) {
    instance->_private.receive.crc = tiny_gea_crc_seed;
  }

  if(instance->_private.receive.count < instance->_private.receive.buffer_size) {
    instance->_private.receive.buffer[instance->_private.receive.count++] = byte;

    instance->_private.receive.crc = tiny_crc16_byte(
      instance->_private.receive.crc,
      byte);
  }
}

static bool received_packet_is_addressed_to_me(self_t* self)
{
  reinterpret(packet, self->_private.receive.buffer, tiny_gea_packet_t*);
  return (packet->destination == self->_private.address) ||
    is_broadcast_address(packet->destination) ||
    self->_private.ignore_destination_address;
}

static void send_ack(self_t* instance, uint8_t address)
{
  if(!is_broadcast_address(address)) {
    tiny_uart_send(instance->_private.uart, tiny_gea_ack);
  }
}

static void process_received_byte(self_t* instance, const uint8_t byte)
{
  reinterpret(packet, instance->_private.receive.buffer, tiny_gea_packet_t*);

  if(instance->_private.receive.escaped) {
    instance->_private.receive.escaped = false;
    buffer_received_byte(instance, byte);
    return;
  }

  switch(byte) {
    case tiny_gea_esc:
      instance->_private.receive.escaped = true;
      break;

    case tiny_gea_stx:
      instance->_private.receive.count = 0;
      break;

    case tiny_gea_etx:
      if(!received_packet_has_minimum_valid_length(instance) || !received_packet_has_valid_length(instance)) {
        break;
      }

      if(!received_packet_has_valid_crc(instance)) {
        break;
      }

      if(!received_packet_is_addressed_to_me(instance)) {
        break;
      }

      packet->payload_length -= tiny_gea_packet_transmission_overhead;
      instance->_private.receive.packet_ready = true;

      send_ack(instance, packet->destination);

      tiny_fsm_transition(&instance->_private.fsm, state_idle_cooldown);
      break;

    default:
      buffer_received_byte(instance, byte);
      break;
  }
}

static tiny_timer_ticks_t get_collision_timeout(uint8_t address, uint8_t pseudo_random_number)
{
  return 43 + (address & 0x1F) + ((pseudo_random_number ^ address) & 0x1F);
}

static void collision_idle_timeout(void* context)
{
  self_t* instance = context;
  tiny_fsm_send_signal(&instance->_private.fsm, signal_collision_idle_timeout, NULL);
}

static void start_collision_idle_timeout_timer(self_t* instance)
{
  uint8_t current_ticks = (uint8_t)tiny_time_source_ticks(instance->_private.timer_group.time_source);
  tiny_timer_ticks_t collision_timeout_ticks = get_collision_timeout(instance->_private.address, current_ticks);

  tiny_timer_start(
    &instance->_private.timer_group,
    &instance->_private.timer,
    collision_timeout_ticks,
    instance,
    collision_idle_timeout);
}

static void state_collision_cooldown(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* instance = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      start_collision_idle_timeout_timer(instance);
      break;

    case signal_collision_idle_timeout:
      tiny_fsm_transition(fsm, state_idle);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      if(*byte == tiny_gea_stx) {
        tiny_fsm_transition(fsm, state_receive);
      }
    } break;
  }
}

static void interbyte_timeout(void* context)
{
  self_t* instance = context;
  tiny_fsm_send_signal(&instance->_private.fsm, signal_interbyte_timeout, NULL);
}

static void start_interbyte_timeout_timer(self_t* instance)
{
  tiny_timer_start(
    &instance->_private.timer_group,
    &instance->_private.timer,
    gea2_interbyte_timeout_msec,
    instance,
    interbyte_timeout);
}

static void state_receive(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* instance = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      instance->_private.receive.count = 0;
      start_interbyte_timeout_timer(instance);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      start_interbyte_timeout_timer(instance);
      process_received_byte(instance, *byte);
      break;
    }

    case signal_interbyte_timeout:
      tiny_fsm_transition(fsm, state_idle_cooldown);
      break;
  }
}

static void idle_cooldown_timeout(void* context)
{
  self_t* instance = context;
  tiny_fsm_send_signal(&instance->_private.fsm, signal_idle_cooldown_timeout, NULL);
}

static tiny_timer_ticks_t get_idle_timeout(uint8_t address)
{
  return 10 + (address & 0x1F);
}

static void state_idle_cooldown(tiny_fsm_t* fsm, const tiny_fsm_signal_t signal, const void* data)
{
  self_t* instance = interface_from_fsm(fsm);

  switch(signal) {
    case tiny_fsm_signal_entry:
      tiny_timer_start(
        &instance->_private.timer_group,
        &instance->_private.timer,
        get_idle_timeout(instance->_private.address),
        instance,
        idle_cooldown_timeout);
      break;

    case signal_byte_received: {
      const uint8_t* byte = data;
      if(*byte == tiny_gea_stx && !instance->_private.receive.packet_ready) {
        tiny_fsm_transition(fsm, state_receive);
      }
      else {
        tiny_fsm_transition(fsm, state_idle_cooldown);
      }

      break;
    }

    case signal_idle_cooldown_timeout:
      tiny_fsm_transition(fsm, state_idle);
      break;
  }
}

static void prepare_buffered_packet_for_transmission(self_t* instance)
{
  reinterpret(send_packet, instance->_private.send.buffer, send_packet_t*);
  send_packet->data_length += tiny_gea_packet_transmission_overhead;
  instance->_private.send.crc = tiny_crc16_block(tiny_gea_crc_seed, (uint8_t*)send_packet, send_packet->data_length - data_length_bytes_not_included_in_data);
  instance->_private.send.state = send_state_stx;
  instance->_private.send.offset = 0;
}

static bool send_worker(
  i_tiny_gea_interface_t* _instance,
  uint8_t destination,
  uint8_t payload_length,
  tiny_gea_interface_send_callback_t callback,
  void* context,
  bool set_source_address)
{
  reinterpret(instance, _instance, self_t*);
  if(instance->_private.send.active) {
    return false;
  }

  if(payload_length + send_packet_header_size > instance->_private.send.buffer_size) {
    return false;
  }

  reinterpret(send_packet, instance->_private.send.buffer, tiny_gea_packet_t*);
  send_packet->payload_length = payload_length;
  callback(context, send_packet);

  if(set_source_address) {
    send_packet->source = instance->_private.address;
  }

  send_packet->destination = destination;
  prepare_buffered_packet_for_transmission(instance);

  instance->_private.send.retries = instance->_private.retries;
  instance->_private.send.active = true;
  instance->_private.send.packet_queued_in_background = true;

  return true;
}

static void msec_interrupt_callback(void* context, const void* _args)
{
  self_t* instance = context;
  (void)_args;

  if(instance->_private.send.packet_queued_in_background) {
    instance->_private.send.packet_queued_in_background = false;
    tiny_fsm_send_signal(&instance->_private.fsm, signal_send_ready, NULL);
  }

  tiny_timer_group_run(&instance->_private.timer_group);
}

static bool send(
  i_tiny_gea_interface_t* _instance,
  uint8_t destination,
  uint8_t payload_length,
  void* context,
  tiny_gea_interface_send_callback_t callback)
{
  return send_worker(_instance, destination, payload_length, callback, context, true);
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

static i_tiny_event_t* get_on_receive_event(i_tiny_gea_interface_t* _instance)
{
  reinterpret(instance, _instance, self_t*);
  return &instance->_private.on_receive.interface;
}

static const i_tiny_gea_interface_api_t api = { send, forward, get_on_receive_event };

void tiny_gea2_interface_init(
  self_t* instance,
  i_tiny_uart_t* uart,
  i_tiny_time_source_t* time_source,
  i_tiny_event_t* msec_interrupt,
  uint8_t* receive_buffer,
  uint8_t receive_buffer_size,
  uint8_t* send_buffer,
  uint8_t send_buffer_size,
  uint8_t address,
  bool ignore_destination_address,
  uint8_t retries)
{
  instance->interface.api = &api;
  instance->_private.uart = uart;
  instance->_private.address = address;
  instance->_private.retries = default_retries;
  instance->_private.ignore_destination_address = ignore_destination_address;
  instance->_private.receive.buffer = receive_buffer;
  instance->_private.receive.buffer_size = receive_buffer_size;
  instance->_private.receive.packet_ready = false;
  instance->_private.receive.escaped = false;
  instance->_private.send.buffer = send_buffer;
  instance->_private.send.buffer_size = send_buffer_size;
  instance->_private.send.active = false;
  instance->_private.send.packet_queued_in_background = false;
  instance->_private.retries = retries;

  tiny_timer_group_init(&instance->_private.timer_group, time_source);

  tiny_event_subscription_init(&instance->_private.byte_received_subscription, instance, byte_received);
  tiny_event_subscribe(tiny_uart_on_receive(uart), &instance->_private.byte_received_subscription);

  tiny_event_subscription_init(&instance->_private.msec_interrupt_subscription, instance, msec_interrupt_callback);
  tiny_event_subscribe(msec_interrupt, &instance->_private.msec_interrupt_subscription);

  tiny_event_init(&instance->_private.on_receive);
  tiny_event_init(&instance->_private.on_diagnostics_event);

  tiny_fsm_init(&instance->_private.fsm, state_idle);
}

void tiny_gea2_interface_run(self_t* instance)
{
  if(instance->_private.receive.packet_ready) {
    tiny_gea_interface_on_receive_args_t args;
    args.packet = (const tiny_gea_packet_t*)instance->_private.receive.buffer;

    tiny_event_publish(&instance->_private.on_receive, &args);
    instance->_private.receive.packet_ready = false;
  }
}
