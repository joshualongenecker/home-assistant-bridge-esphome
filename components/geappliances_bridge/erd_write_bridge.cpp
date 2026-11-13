
#include "erd_write_bridge.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include "tiny_gea_constants.h"
#include "erd_bridge_common.h"

enum {
  signal_write_requested = tiny_hsm_signal_user_start,
  signal_write_completed,
  signal_write_failed
};

GEA_TAG(TAG) = "erd_write_bridge";

static tiny_hsm_result_t write_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_ready(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_writing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

static void start_next_write(erd_write_bridge_t* self);

static tiny_hsm_result_t write_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)hsm;
  (void)signal;
  (void)data;
  return tiny_hsm_result_signal_deferred;
}

static void report_write_failure(erd_write_bridge_t* self, tiny_erd_t erd, uint8_t board_address,
  tiny_gea3_erd_client_write_failure_reason_t reason)
{
  mqtt_client_update_erd_write_result(self->mqtt_client, erd, board_address, false, reason);
}

/*!
 * Pop the head of the queue and put it on the bus, then stay in
 * state_writing until the ERD client reports the result. Requests the ERD
 * client cannot accept are reported here (they never become in-flight), and
 * the next queued request is tried so one bad request cannot stall the queue.
 *
 * Does nothing while the queue is empty; the bridge is then idle in
 * state_ready.
 */
static void start_next_write(erd_write_bridge_t* self)
{
  while(self->queue_count > 0) {
    erd_write_request_t* request = &self->queue[self->queue_head];

    // Primary-board topics (board_address == 0xFF, the broadcast sentinel)
    // route to the detected host address; secondary-board topics route to
    // the parsed address.
    uint8_t target_address = (request->board_address != tiny_gea_broadcast_address)
      ? request->board_address
      : self->erd_host_address;

    tiny_gea3_erd_client_request_id_t request_id;
    if(tiny_gea3_erd_client_write(self->erd_client, &request_id, target_address,
       request->erd, request->value, request->size)) {
      self->pending_request_id = request_id;
      self->pending_erd = request->erd;
      self->pending_board_address = request->board_address;

      self->queue_head = (self->queue_head + 1) % erd_write_bridge_queue_capacity;
      self->queue_count--;

      tiny_hsm_transition(&self->hsm, state_writing);
      return;
    }

    // Copy the identity out before popping: popping only moves the head, but
    // the next iteration may overwrite this slot.
    tiny_erd_t failed_erd = request->erd;
    uint8_t failed_board_address = request->board_address;

    self->queue_head = (self->queue_head + 1) % erd_write_bridge_queue_capacity;
    self->queue_count--;

    ESP_LOGW(TAG, "Write request for ERD 0x%04x failed to queue", failed_erd);
    report_write_failure(self, failed_erd, failed_board_address,
      tiny_gea3_erd_client_write_failure_reason_retries_exhausted);
  }
}

/*!
 * Queue a write request, or reject it with a write_result if it cannot be
 * honored. Whether the new request goes straight onto the bus is the caller's
 * decision: only `state_ready` pumps the queue, so a request that arrives
 * while a write is in flight always waits its turn.
 *
 * The payload has to be copied: the event args point at the MQTT client's
 * stack buffer, which is only valid for the duration of the event.
 */
static void enqueue_write_request(erd_write_bridge_t* self, const mqtt_client_on_write_request_args_t* args)
{
  if(self->erd_host_address == tiny_gea_broadcast_address) {
    ESP_LOGW(TAG, "Write request for ERD 0x%04x dropped: appliance not identified", args->erd);
    report_write_failure(self, args->erd, args->board_address,
      tiny_gea3_erd_client_write_failure_reason_not_supported);
    return;
  }

  if(args->size > ERD_WRITE_MAX_DATA_SIZE) {
    ESP_LOGW(TAG, "Write request for ERD 0x%04x dropped: %u byte payload exceeds the %u byte limit",
      args->erd, (unsigned)args->size, (unsigned)ERD_WRITE_MAX_DATA_SIZE);
    report_write_failure(self, args->erd, args->board_address,
      tiny_gea3_erd_client_write_failure_reason_incorrect_size);
    return;
  }

  if(self->queue_count == erd_write_bridge_queue_capacity) {
    ESP_LOGW(TAG, "Write request for ERD 0x%04x dropped: write queue full (%u pending)",
      args->erd, (unsigned)self->queue_count);
    report_write_failure(self, args->erd, args->board_address,
      tiny_gea3_erd_client_write_failure_reason_retries_exhausted);
    return;
  }

  erd_write_request_t* slot = &self->queue[(self->queue_head + self->queue_count) % erd_write_bridge_queue_capacity];
  slot->erd = args->erd;
  slot->board_address = args->board_address;
  slot->size = args->size;
  if(args->value != nullptr && args->size > 0) {
    memcpy(slot->value, args->value, args->size);
  }

  self->queue_count++;
}

static tiny_hsm_result_t state_ready(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_write_bridge_t* self = container_of(erd_write_bridge_t, hsm, hsm);

  switch(signal) {
    case signal_write_requested:
      // Nothing is in flight, so the queue holds only this request: it goes
      // out immediately.
      enqueue_write_request(self, reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data));
      start_next_write(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_writing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_write_bridge_t* self = container_of(erd_write_bridge_t, hsm, hsm);

  switch(signal) {
    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);
      // The bus is busy with the previous write; this one waits its turn
      // unless the queue is already full, in which case it is rejected with a
      // write_result rather than dropped silently.
      enqueue_write_request(self, args);
    } break;

    case signal_write_completed: {
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      if (args->write_completed.request_id != self->pending_request_id) {
        ESP_LOGW(TAG, "Stale write completion for request_id %u (expected %u); ignoring",
          args->write_completed.request_id, self->pending_request_id);
        break;
      }
      mqtt_client_update_erd_write_result(self->mqtt_client, self->pending_erd, self->pending_board_address, true,
        0);
      tiny_hsm_transition(hsm, state_ready);
      start_next_write(self);
    } break;

    case signal_write_failed: {
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      if (args->write_failed.request_id != self->pending_request_id) {
        ESP_LOGW(TAG, "Stale write failure for request_id %u (expected %u); ignoring",
          args->write_failed.request_id, self->pending_request_id);
        break;
      }
      mqtt_client_update_erd_write_result(self->mqtt_client, self->pending_erd, self->pending_board_address, false,
        args->write_failed.reason);
      tiny_hsm_transition(hsm, state_ready);
      start_next_write(self);
    } break;
    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static const tiny_hsm_state_descriptor_t write_hsm_state_descriptors[] = {
  { .state = write_state_top, .parent = nullptr },
  { .state = state_ready, .parent = write_state_top },
  { .state = state_writing, .parent = write_state_top }
};
static const tiny_hsm_configuration_t write_hsm_configuration = {
  .states = write_hsm_state_descriptors,
  .state_count = element_count(write_hsm_state_descriptors)
};

void erd_write_bridge_init(
  erd_write_bridge_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint8_t host_address)
{
  self->timer_group = timer_group;
  self->erd_client = erd_client;
  self->mqtt_client = mqtt_client;
  self->erd_host_address = host_address;
  self->queue_head = 0;
  self->queue_count = 0;
  self->pending_request_id = 0;
  self->pending_erd = 0;
  self->pending_board_address = tiny_gea_broadcast_address;

  tiny_event_subscription_init(
    &self->mqtt_write_request_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<erd_write_bridge_t*>(context);
      tiny_hsm_send_signal(&self->hsm, signal_write_requested, _args);
    });
  tiny_event_subscribe(mqtt_client_on_write_request(mqtt_client), &self->mqtt_write_request_subscription);

  // Subscribe to ERD client activity for write results
  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<erd_write_bridge_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      switch(args->type) {
        case tiny_gea3_erd_client_activity_type_write_completed:
          tiny_hsm_send_signal(&self->hsm, signal_write_completed, args);
          break;
        case tiny_gea3_erd_client_activity_type_write_failed:
          tiny_hsm_send_signal(&self->hsm, signal_write_failed, args);
          break;
        default:
          break;
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  tiny_hsm_init(&self->hsm, &write_hsm_configuration, state_ready);
}

void erd_write_bridge_destroy(erd_write_bridge_t* self)
{
  // Guard against destroy() being called on a never-initialized struct.
  if(!self->timer_group) {
    return;
  }

  // Guard against partial init where mqtt_client or erd_client may be null.
  if (self->mqtt_client) {
    tiny_event_unsubscribe(mqtt_client_on_write_request(self->mqtt_client),
      &self->mqtt_write_request_subscription);
  }
  if (self->erd_client) {
    tiny_event_unsubscribe(tiny_gea3_erd_client_on_activity(self->erd_client),
      &self->erd_client_activity_subscription);
  }
}
