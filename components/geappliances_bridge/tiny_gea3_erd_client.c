/*!
 * @file
 * @brief
 */

#include <stddef.h>
#include <string.h>
#include "tiny_gea3_erd_api.h"
#include "tiny_gea3_erd_client.h"
#include "tiny_gea_constants.h"
#include "tiny_stack_allocator.h"
#include "tiny_utils.h"

enum {
  request_type_read,
  request_type_write,
  request_type_subscribe,
  request_type_invalid
};
typedef uint8_t request_type_t;

typedef struct {
  request_type_t type;
} request_t;

// These request types need to have no padding _or_ we need to memset them to 0
// since we memcmp the requests to detect duplicates

typedef struct {
  request_type_t type;
  uint8_t address;
  tiny_erd_t erd;
} read_request_t;

typedef struct {
  request_type_t type;
  uint8_t address;
  tiny_erd_t erd;
  uint8_t data_size;
  uint8_t data[1];
} write_request_t;

typedef struct {
  request_type_t type;
  uint8_t address;
  bool retain;
} subscribe_request_t;

typedef bool (*requests_conflict_predicate_t)(const request_t* new_request, const request_t* queued_request);

typedef tiny_gea3_erd_client_t self_t;

typedef struct {
  self_t* self;
  read_request_t* request;
} read_request_worker_context_t;

static bool valid_read_request(const tiny_gea_packet_t* packet)
{
  return packet->payload_length == sizeof(tiny_gea3_erd_api_read_request_payload_t);
}

static bool valid_read_response(const tiny_gea_packet_t* packet)
{
  reinterpret(payload, packet->payload, const tiny_gea3_erd_api_read_response_payload_t*);

  if((payload->header.result != tiny_gea3_erd_api_read_result_success) && (packet->payload_length == offsetof(tiny_gea3_erd_api_read_response_payload_header_t, data_size))) {
    return true;
  }

  return (packet->payload_length >= sizeof(tiny_gea3_erd_api_read_response_payload_header_t)) && (packet->payload_length == (sizeof(tiny_gea3_erd_api_read_response_payload_header_t) + payload->header.data_size));
}

static bool valid_write_request(const tiny_gea_packet_t* packet)
{
  return (packet->payload_length >= 5) && (packet->payload_length == (5 + packet->payload[4]));
}

static bool valid_write_response(const tiny_gea_packet_t* packet)
{
  return packet->payload_length == sizeof(tiny_gea3_erd_api_write_response_payload_t);
}

static bool valid_subscribe_all_request(const tiny_gea_packet_t* packet)
{
  reinterpret(payload, packet->payload, const tiny_gea3_erd_api_subscribe_all_request_payload_t*);

  if(packet->payload_length != 3) {
    return false;
  }

  switch(payload->type) {
    case tiny_gea3_erd_api_subscribe_all_request_type_add_subscription:
    case tiny_gea3_erd_api_subscribe_all_request_type_retain_subscription:
      break;

    default:
      return false;
  }

  return true;
}

static bool valid_subscribe_all_response(const tiny_gea_packet_t* packet)
{
  reinterpret(payload, packet->payload, const tiny_gea3_erd_api_subscribe_all_response_payload_t*);

  if(packet->payload_length != sizeof(tiny_gea3_erd_api_subscribe_all_response_payload_t)) {
    return false;
  }

  switch(payload->result) {
    case tiny_gea3_erd_api_subscribe_all_result_success:
    case tiny_gea3_erd_api_subscribe_all_result_no_available_subscriptions:
      break;

    default:
      return false;
  }

  return true;
}

static bool valid_subscription_publication(const tiny_gea_packet_t* packet)
{
  if(packet->payload_length < 4) {
    return false;
  }

  const uint8_t claimed_count = packet->payload[3];
  uint8_t actual_count = 0;
  uint8_t index = 4;

  while(index < packet->payload_length) {
    index += sizeof(tiny_erd_t);

    if(index >= packet->payload_length) {
      return false;
    }

    uint8_t size = packet->payload[index++];
    index += size;

    if(index <= packet->payload_length) {
      actual_count++;
    }
  }

  return actual_count == claimed_count;
}

static bool valid_subscription_publication_acknowledgment(const tiny_gea_packet_t* packet)
{
  return packet->payload_length == sizeof(tiny_gea3_erd_api_publication_acknowledgement_payload_t);
}

static bool valid_subscription_host_startup(const tiny_gea_packet_t* packet)
{
  return packet->payload_length == 1;
}

static bool packet_is_valid(const tiny_gea_packet_t* packet)
{
  if(packet->payload_length < 1) {
    return false;
  }

  switch(packet->payload[0]) {
    case tiny_gea3_erd_api_command_read_request:
      return valid_read_request(packet);

    case tiny_gea3_erd_api_command_read_response:
      return valid_read_response(packet);

    case tiny_gea3_erd_api_command_write_request:
      return valid_write_request(packet);

    case tiny_gea3_erd_api_command_write_response:
      return valid_write_response(packet);

    case tiny_gea3_erd_api_command_subscribe_all_request:
      return valid_subscribe_all_request(packet);

    case tiny_gea3_erd_api_command_subscribe_all_response:
      return valid_subscribe_all_response(packet);

    case tiny_gea3_erd_api_command_publication:
      return valid_subscription_publication(packet);

    case tiny_gea3_erd_api_command_publication_acknowledgment:
      return valid_subscription_publication_acknowledgment(packet);

    case tiny_gea3_erd_api_command_subscription_host_startup:
      return valid_subscription_host_startup(packet);
  }

  return false;
}

static void send_read_request_worker(void* _context, tiny_gea_packet_t* packet)
{
  read_request_worker_context_t* context = _context;
  self_t* self = context->self;
  read_request_t* request = context->request;
  reinterpret(read_request_payload, packet->payload, tiny_gea3_erd_api_read_request_payload_t*);

  read_request_payload->command = tiny_gea3_erd_api_command_read_request;
  read_request_payload->request_id = self->request_id;
  read_request_payload->erd_msb = request->erd >> 8;
  read_request_payload->erd_lsb = request->erd & 0xFF;
}

static void send_read_request(self_t* self)
{
  read_request_t request;
  uint16_t size;

  tiny_queue_peek(&self->request_queue, &request, &size, 0);

  read_request_worker_context_t context = { self, &request };

  tiny_gea_interface_send(
    self->gea3_interface,
    request.address,
    sizeof(tiny_gea3_erd_api_read_request_payload_t),
    &context,
    send_read_request_worker);
}

static void send_write_request_worker(void* _context, tiny_gea_packet_t* packet)
{
  reinterpret(self, _context, self_t*);

  write_request_t request;
  tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);

  uint16_t size;
  tiny_queue_peek(&self->request_queue, (uint8_t*)packet + 3, &size, 0);

  packet->destination = request.address;
  packet->payload[0] = tiny_gea3_erd_api_command_write_request;
  packet->payload[1] = self->request_id;
  packet->payload[2] = request.erd >> 8;
  packet->payload[3] = request.erd & 0xFF;
  packet->payload[4] = request.data_size;
}

static void send_write_request(self_t* self)
{
  write_request_t request;
  tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);

  tiny_gea_interface_send(
    self->gea3_interface,
    request.address,
    sizeof(tiny_gea3_erd_api_write_request_payload_header_t) + request.data_size,
    self,
    send_write_request_worker);
}

typedef struct {
  self_t* self;
  subscribe_request_t* request;
} subscribe_request_worker_context_t;

static void send_subscribe_request_worker(void* _context, tiny_gea_packet_t* packet)
{
  subscribe_request_worker_context_t* context = _context;
  self_t* self = context->self;
  subscribe_request_t* request = context->request;
  reinterpret(subscribe_all_request_payload, packet->payload, tiny_gea3_erd_api_subscribe_all_request_payload_t*);

  subscribe_all_request_payload->command = tiny_gea3_erd_api_command_subscribe_all_request;
  subscribe_all_request_payload->request_id = self->request_id;
  subscribe_all_request_payload->type = request->retain ? tiny_gea3_erd_api_subscribe_all_request_type_retain_subscription : tiny_gea3_erd_api_subscribe_all_request_type_add_subscription;
}

static void send_subscribe_request(self_t* self)
{
  subscribe_request_t request;
  uint16_t size;

  tiny_queue_peek(&self->request_queue, &request, &size, 0);

  subscribe_request_worker_context_t context = { self, &request };

  tiny_gea_interface_send(
    self->gea3_interface,
    request.address,
    sizeof(tiny_gea3_erd_api_subscribe_all_request_payload_t),
    &context,
    send_subscribe_request_worker);
}

static void resend_request(self_t* self);

static void request_timed_out(void* context)
{
  reinterpret(self, context, self_t*);
  resend_request(self);
}

static void arm_request_timeout(self_t* self)
{
  tiny_timer_start(
    self->timer_group,
    &self->request_retry_timer,
    self->configuration->request_timeout,
    self,
    request_timed_out);
}

static void disarm_request_timeout(self_t* self)
{
  tiny_timer_stop(
    self->timer_group,
    &self->request_retry_timer);
}

static bool request_pending(self_t* self)
{
  return tiny_queue_count(&self->request_queue) > 0;
}

static request_type_t request_type(self_t* self)
{
  if(request_pending(self)) {
    request_t request;
    tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);
    return request.type;
  }
  else {
    return request_type_invalid;
  }
}

static void send_request(self_t* self)
{
  switch(request_type(self)) {
    case request_type_read:
      send_read_request(self);
      break;

    case request_type_write:
      send_write_request(self);
      break;

    case request_type_subscribe:
      send_subscribe_request(self);
      break;
  }

  arm_request_timeout(self);
}

static void send_request_if_not_busy(self_t* self)
{
  if(!self->busy && request_pending(self)) {
    self->busy = true;
    self->remaining_retries = self->configuration->request_retries;
    send_request(self);
  }
}

static void finish_request(self_t* self)
{
  tiny_queue_discard(&self->request_queue);
  disarm_request_timeout(self);
  self->request_id++;
  self->busy = false;
  send_request_if_not_busy(self);
}

static void handle_read_failure(self_t* self, tiny_gea3_erd_client_read_failure_reason_t reason)
{
  read_request_t request;
  uint16_t size;
  tiny_queue_peek(&self->request_queue, &request, &size, 0);

  tiny_gea3_erd_client_on_activity_args_t args;
  args.address = request.address;
  args.type = tiny_gea3_erd_client_activity_type_read_failed;
  args.read_failed.erd = request.erd;
  args.read_failed.request_id = self->request_id;
  args.read_failed.reason = reason;

  finish_request(self);

  tiny_event_publish(&self->on_activity, &args);
}

typedef struct {
  self_t* self;
  tiny_gea3_erd_client_write_failure_reason_t reason;
} handle_write_failure_context_t;

static void HandleWriteFailureWorker(void* _context, void* allocated_block)
{
  reinterpret(context, _context, handle_write_failure_context_t*);
  reinterpret(request, allocated_block, write_request_t*);

  uint16_t size;
  tiny_queue_peek(&context->self->request_queue, request, &size, 0);

  tiny_gea3_erd_client_on_activity_args_t args;
  args.address = request->address;
  args.type = tiny_gea3_erd_client_activity_type_write_failed;
  args.write_failed.request_id = context->self->request_id;
  args.write_failed.erd = request->erd;
  args.write_failed.data = request->data;
  args.write_failed.data_size = request->data_size;
  args.write_failed.reason = context->reason;

  finish_request(context->self);

  tiny_event_publish(&context->self->on_activity, &args);
}

static void handle_write_failure(self_t* self, tiny_gea3_erd_client_write_failure_reason_t reason)
{
  write_request_t request;
  tiny_queue_peek_partial(&self->request_queue, &request, offsetof(write_request_t, data), 0);

  handle_write_failure_context_t context = { self, reason };
  tiny_stack_allocator_allocate_aligned(request.data_size + offsetof(write_request_t, data), &context, HandleWriteFailureWorker);
}

static void handle_subscribe_failure(self_t* self)
{
  read_request_t request;
  uint16_t size;
  tiny_queue_peek(&self->request_queue, &request, &size, 0);

  tiny_gea3_erd_client_on_activity_args_t args;
  args.address = request.address;
  args.type = tiny_gea3_erd_client_activity_type_subscribe_failed;

  finish_request(self);

  tiny_event_publish(&self->on_activity, &args);
}

static void fail_request(self_t* self, uint8_t reason)
{
  switch(request_type(self)) {
    case request_type_read:
      handle_read_failure(self, reason);
      break;

    case request_type_write:
      handle_write_failure(self, reason);
      break;

    case request_type_subscribe:
      handle_subscribe_failure(self);
      break;
  }
}

static void resend_request(self_t* self)
{
  if(self->remaining_retries > 0) {
    self->remaining_retries--;
    send_request(self);
  }
  else {
    fail_request(self, tiny_gea3_erd_client_read_failure_reason_retries_exhausted);
  }
}

static void handle_read_response_packet(self_t* self, const tiny_gea_packet_t* packet)
{
  if(request_type(self) == request_type_read) {
    read_request_t request;
    tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);

    reinterpret(payload, packet->payload, const tiny_gea3_erd_api_read_response_payload_t*);
    tiny_erd_t erd = (payload->header.erd_msb << 8) + payload->header.erd_lsb;
    tiny_gea3_erd_api_request_id_t request_id = payload->header.request_id;
    tiny_gea3_erd_api_read_result_t result = payload->header.result;

    if((self->request_id == request_id) &&
      ((request.address == packet->source) || (request.address == tiny_gea_broadcast_address)) && (request.erd == erd)) {
      if(result == tiny_gea3_erd_api_read_result_success) {
        tiny_gea3_erd_client_on_activity_args_t args;
        args.address = packet->source;
        args.type = tiny_gea3_erd_client_activity_type_read_completed;
        args.read_completed.request_id = request_id;
        args.read_completed.erd = erd;
        args.read_completed.data_size = payload->header.data_size;
        args.read_completed.data = payload->data;

        finish_request(self);

        tiny_event_publish(&self->on_activity, &args);
      }
      else if(result == tiny_gea3_erd_api_read_result_unsupported_erd) {
        fail_request(self, tiny_gea3_erd_client_read_failure_reason_not_supported);
      }
    }
  }
}

typedef struct {
  self_t* self;
  uint8_t clientAddress;
} handle_write_response_packet_context_t;

static void handle_write_response_packet_worker(void* _context, void* allocated_block)
{
  reinterpret(context, _context, handle_write_response_packet_context_t*);
  reinterpret(request, allocated_block, write_request_t*);

  uint16_t size;
  tiny_queue_peek(&context->self->request_queue, request, &size, 0);

  tiny_gea3_erd_client_on_activity_args_t args;
  args.address = context->clientAddress;
  args.type = tiny_gea3_erd_client_activity_type_write_completed;
  args.write_completed.request_id = context->self->request_id;
  args.write_completed.erd = request->erd;
  args.write_completed.data = request->data;
  args.write_completed.data_size = request->data_size;

  finish_request(context->self);

  tiny_event_publish(&context->self->on_activity, &args);
}

static void handle_write_response_packet(self_t* self, const tiny_gea_packet_t* packet)
{
  if(request_type(self) == request_type_write) {
    write_request_t request;
    tiny_queue_peek_partial(&self->request_queue, &request, offsetof(write_request_t, data), 0);

    reinterpret(payload, packet->payload, const tiny_gea3_erd_api_write_response_payload_t*);
    tiny_erd_t erd = (payload->erd_msb << 8) + payload->erd_lsb;
    tiny_gea3_erd_api_request_id_t request_id = payload->request_id;
    tiny_gea3_erd_api_write_result_t result = payload->result;

    if((self->request_id == request_id) &&
      ((request.address == packet->source) || (request.address == tiny_gea_broadcast_address)) &&
      (request.erd == erd)) {
      if(result == tiny_gea3_erd_api_write_result_success) {
        handle_write_response_packet_context_t context = { self, .clientAddress = packet->source };
        tiny_stack_allocator_allocate_aligned(request.data_size + offsetof(write_request_t, data), &context, handle_write_response_packet_worker);
      }
      else if(result == tiny_gea3_erd_api_write_result_incorrect_size) {
        fail_request(self, tiny_gea3_erd_client_write_failure_reason_incorrect_size);
      }
      else if(result == tiny_gea3_erd_api_write_result_unsupported_erd) {
        fail_request(self, tiny_gea3_erd_client_write_failure_reason_not_supported);
      }
    }
  }
}

static void handle_subscribe_all_response_packet(self_t* self, const tiny_gea_packet_t* packet)
{
  if(request_type(self) == request_type_subscribe) {
    subscribe_request_t request;
    tiny_queue_peek_partial(&self->request_queue, &request, sizeof(request), 0);

    reinterpret(payload, packet->payload, const tiny_gea3_erd_api_subscribe_all_response_payload_t*);
    tiny_gea3_erd_api_request_id_t request_id = payload->request_id;
    tiny_gea3_erd_api_subscribe_all_result_t result = payload->result;

    if((self->request_id == request_id) &&
      (request.address == packet->source)) {
      if(result == tiny_gea3_erd_api_subscribe_all_result_success) {
        tiny_gea3_erd_client_on_activity_args_t args;
        args.address = packet->source;
        args.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;

        finish_request(self);

        tiny_event_publish(&self->on_activity, &args);
      }
      else {
        handle_subscribe_failure(self);
      }
    }
  }
}

typedef struct {
  uint8_t _context;
  uint8_t request_id;
} subscription_publication_acknowledgment_worker_context_t;

static void send_subscription_publication_acknowledgment_worker(void* _context, tiny_gea_packet_t* packet)
{
  subscription_publication_acknowledgment_worker_context_t* context = _context;
  reinterpret(payload, packet->payload, tiny_gea3_erd_api_publication_acknowledgement_payload_t*);

  payload->command = tiny_gea3_erd_api_command_publication_acknowledgment;
  payload->context = context->_context;
  payload->request_id = context->request_id;
}

static void send_subscription_publication_acknowledgment(self_t* self, uint8_t address, uint8_t _context, uint8_t request_id)
{
  subscription_publication_acknowledgment_worker_context_t context = { _context, request_id };

  tiny_gea_interface_send(
    self->gea3_interface,
    address,
    sizeof(tiny_gea3_erd_api_publication_acknowledgement_payload_t),
    &context,
    send_subscription_publication_acknowledgment_worker);
}

static void handle_subscription_publication_packet(self_t* self, const tiny_gea_packet_t* packet)
{
  reinterpret(payload, packet->payload, const tiny_gea3_erd_api_publication_header_t*);

  uint8_t count = payload->erd_count;
  uint8_t offset = sizeof(*payload);

  for(uint8_t i = 0; i < count; i++) {
    tiny_erd_t erd = (packet->payload[offset++] << 8);
    erd += packet->payload[offset++];

    uint8_t data_size = packet->payload[offset++];

    tiny_gea3_erd_client_on_activity_args_t args;
    args.address = packet->source;
    args.type = tiny_gea3_erd_client_activity_type_subscription_publication_received;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data_size = data_size;
    args.subscription_publication_received.data = &packet->payload[offset];
    tiny_event_publish(&self->on_activity, &args);

    offset += data_size;
  }

  uint8_t address = packet->source;
  uint8_t context = payload->context;
  uint8_t request_id = payload->request_id;
  send_subscription_publication_acknowledgment(self, address, context, request_id);
}

static void handle_subscription_host_startup_packet(self_t* self, const tiny_gea_packet_t* packet)
{
  tiny_gea3_erd_client_on_activity_args_t args;
  args.address = packet->source;
  args.type = tiny_gea3_erd_client_activity_type_subscription_host_came_online;

  tiny_event_publish(&self->on_activity, &args);
}

static void packet_received(void* _self, const void* _args)
{
  reinterpret(self, _self, self_t*);
  reinterpret(args, _args, const tiny_gea_interface_on_receive_args_t*);

  if(!packet_is_valid(args->packet)) {
    return;
  }

  switch(args->packet->payload[0]) {
    case tiny_gea3_erd_api_command_read_response:
      handle_read_response_packet(self, args->packet);
      break;

    case tiny_gea3_erd_api_command_write_response:
      handle_write_response_packet(self, args->packet);
      break;

    case tiny_gea3_erd_api_command_subscribe_all_response:
      handle_subscribe_all_response_packet(self, args->packet);
      break;

    case tiny_gea3_erd_api_command_publication:
      handle_subscription_publication_packet(self, args->packet);
      break;

    case tiny_gea3_erd_api_command_subscription_host_startup:
      handle_subscription_host_startup_packet(self, args->packet);
      break;
  }
}

typedef struct {
  self_t* self;
  const void* request;
  requests_conflict_predicate_t requests_conflict_predicate;
  uint16_t i;
  uint16_t request_size;
  bool request_already_queued;
  bool request_conflict_found;
} enqueue_request_if_unique_context_t;

static void enqueue_request_if_unique_worker(void* _context, void* allocated_block)
{
  reinterpret(context, _context, enqueue_request_if_unique_context_t*);

  uint16_t size;
  tiny_queue_peek(&context->self->request_queue, allocated_block, &size, context->i);

  context->request_already_queued =
    (context->request_size == size) &&
    (memcmp(context->request, allocated_block, size) == 0);

  if(context->requests_conflict_predicate) {
    context->request_conflict_found = context->requests_conflict_predicate(context->request, allocated_block);
  }
}

static bool enqueue_request_if_unique(
  self_t* self,
  const void* request,
  uint16_t request_size,
  uint16_t* index,
  requests_conflict_predicate_t requests_conflict_predicate)
{
  uint16_t count = tiny_queue_count(&self->request_queue);

  for(uint16_t counter = count; counter > 0; counter--) {
    uint16_t i = counter - 1;

    uint16_t elementSize;
    tiny_queue_peek_size(&self->request_queue, &elementSize, i);

    enqueue_request_if_unique_context_t context;
    context.self = self;
    context.request = request;
    context.request_size = request_size;
    context.requests_conflict_predicate = requests_conflict_predicate;
    context.request_conflict_found = false;
    context.i = i;

    tiny_stack_allocator_allocate_aligned(elementSize, &context, enqueue_request_if_unique_worker);

    if(context.request_already_queued) {
      *index = i;
      return true;
    }

    if(context.request_conflict_found) {
      break;
    }
  }

  *index = count;
  return tiny_queue_enqueue(&self->request_queue, request, request_size);
}

static bool read_request_conflicts(const request_t* new_request, const request_t* queued_request)
{
  (void)new_request;

  switch(queued_request->type) {
    case request_type_write:
      return true;

    default:
      return false;
  }
}

static bool read(i_tiny_gea3_erd_client_t* _self, tiny_gea3_erd_client_request_id_t* request_id, uint8_t address, tiny_erd_t erd)
{
  reinterpret(self, _self, self_t*);

  uint16_t index;
  read_request_t request;
  request.type = request_type_read;
  request.address = address;
  request.erd = erd;
  bool request_added_or_already_queued = enqueue_request_if_unique(
    self,
    &request,
    sizeof(request),
    &index,
    read_request_conflicts);

  *request_id = index + self->request_id;

  send_request_if_not_busy(self);

  return request_added_or_already_queued;
}

typedef struct {
  self_t* self;
  const void* data;
  tiny_erd_t erd;
  uint16_t index;
  tiny_gea3_erd_client_request_id_t request_id;
  uint8_t address;
  uint8_t data_size;
  bool request_added_or_already_queued;
} write_context_t;

static bool write_request_conflicts(const request_t* new_request, const request_t* queued_request)
{
  (void)new_request;

  switch(queued_request->type) {
    case request_type_write:
    case request_type_read:
      return true;

    default:
      return false;
  }
}

static void write_worker(void* _context, void* allocated_block)
{
  reinterpret(context, _context, write_context_t*);
  reinterpret(request, allocated_block, write_request_t*);

  request->type = request_type_write;
  request->address = context->address;
  request->erd = context->erd;
  request->data_size = context->data_size;
  memcpy(request->data, context->data, context->data_size);
  context->request_added_or_already_queued = enqueue_request_if_unique(
    context->self,
    request,
    offsetof(write_request_t, data) + context->data_size,
    &context->index,
    write_request_conflicts);

  context->request_id = context->index + context->self->request_id;

  send_request_if_not_busy(context->self);
}

static bool write(i_tiny_gea3_erd_client_t* _self, tiny_gea3_erd_client_request_id_t* request_id, uint8_t address, tiny_erd_t erd, const void* data, uint8_t data_size)
{
  reinterpret(self, _self, self_t*);

  write_context_t context;
  context.self = self;
  context.address = address;
  context.erd = erd;
  context.data = data;
  context.data_size = data_size;

  tiny_stack_allocator_allocate_aligned(offsetof(write_request_t, data) + data_size, &context, write_worker);

  *request_id = context.request_id;

  return context.request_added_or_already_queued;
}

static bool subscribe_or_retain(self_t* self, uint8_t address, bool retain)
{
  uint16_t dummyIndex;
  subscribe_request_t request;
  request.type = request_type_subscribe;
  request.address = address;
  request.retain = retain;
  bool request_added_or_already_queued = enqueue_request_if_unique(self, &request, sizeof(request), &dummyIndex, NULL);

  send_request_if_not_busy(self);

  return request_added_or_already_queued;
}

static bool subscribe(i_tiny_gea3_erd_client_t* _self, uint8_t address)
{
  reinterpret(self, _self, self_t*);
  return subscribe_or_retain(self, address, false);
}

static bool retain_subscription(i_tiny_gea3_erd_client_t* _self, uint8_t address)
{
  reinterpret(self, _self, self_t*);
  return subscribe_or_retain(self, address, true);
}

static i_tiny_event_t* on_activity(i_tiny_gea3_erd_client_t* _self)
{
  reinterpret(self, _self, self_t*);
  return &self->on_activity.interface;
}

static const i_tiny_gea3_erd_client_api_t api = { read, write, subscribe, retain_subscription, on_activity };

void tiny_gea3_erd_client_init(
  tiny_gea3_erd_client_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea_interface_t* gea3_interface,
  uint8_t* queue_buffer,
  size_t queue_buffer_size,
  const tiny_gea3_erd_client_configuration_t* configuration)
{
  self->interface.api = &api;

  self->request_id = 0;
  self->busy = false;
  self->gea3_interface = gea3_interface;
  self->configuration = configuration;
  self->timer_group = timer_group;

  tiny_queue_init(&self->request_queue, queue_buffer, queue_buffer_size);

  tiny_event_init(&self->on_activity);

  tiny_event_subscription_init(&self->packet_received, self, packet_received);
  tiny_event_subscribe(tiny_gea_interface_on_receive(gea3_interface), &self->packet_received);
}
