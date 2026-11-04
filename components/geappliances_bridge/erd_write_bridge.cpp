
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

static tiny_hsm_result_t write_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)hsm;
  (void)signal;
  (void)data;
  return tiny_hsm_result_signal_deferred;
}

static tiny_hsm_result_t state_ready(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_write_bridge_t* self = container_of(erd_write_bridge_t, hsm, hsm);

  switch(signal) {
    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);

      if(self->erd_host_address == tiny_gea_broadcast_address) {
        ESP_LOGW(TAG, "Write request for ERD 0x%04x dropped: appliance not identified", args->erd);
        mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
          tiny_gea3_erd_client_write_failure_reason_not_supported);
        break;
      }

      // Primary-board topics (board_address == 0xFF, the broadcast sentinel)
      // route to the detected host address; secondary-board topics route to
      // the parsed address.
      uint8_t target_address = (args->board_address != tiny_gea_broadcast_address) ? args->board_address : self->erd_host_address;

      tiny_gea3_erd_client_request_id_t request_id;
      if(!tiny_gea3_erd_client_write(self->erd_client, &request_id, target_address,
         args->erd, args->value, args->size)) {
        ESP_LOGW(TAG, "Write request for ERD 0x%04x failed to queue", args->erd);
        mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
          tiny_gea3_erd_client_write_failure_reason_retries_exhausted);
        break;
      }

      self->pending_request_id = request_id;
      self->pending_erd = args->erd;
      self->pending_board_address = args->board_address;
      tiny_hsm_transition(hsm, state_writing);
    } break;

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
      [[maybe_unused]] auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);
      ESP_LOGW(TAG, "Write request for ERD 0x%04x dropped: write already in progress", args->erd);
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
