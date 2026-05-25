/*!
 * @file
 * @brief MQTT subscription bridge implementation.
 *
 * Manages the GEA3 ERD subscription lifecycle: subscribing, retaining the
 * subscription every 30 s, and publishing received ERD values via MQTT.
 * The polling bridge lives in mqtt_bridge_polling.cpp; shared signals and
 * utility templates are in mqtt_bridge_common.h.
 */

#include "mqtt_bridge_common.h"
#include "esphome/core/log.h"

#include <set>

using namespace std;

static const char* const TAG __attribute__((unused)) = "mqtt_bridge";

// ============================================================================
// Subscription bridge
// ============================================================================

static tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_subscribing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

static tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_t* self = container_of(mqtt_bridge_t, hsm, hsm);

  switch(signal) {
    case signal_subscription_publication_received: {
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      auto erd = args->subscription_publication_received.erd;

      if(erd_set(self).find(erd) == erd_set(self).end()) {
        mqtt_client_register_erd(self->mqtt_client, erd);
        erd_set(self).insert(erd);
      }

      mqtt_client_update_erd(
        self->mqtt_client,
        erd,
        args->subscription_publication_received.data,
        args->subscription_publication_received.data_size);
    } break;

    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);
      tiny_gea3_erd_client_request_id_t request_id;
      tiny_gea3_erd_client_write(self->erd_client, &request_id, self->erd_host_address, args->erd, args->value, args->size);
    } break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_subscribing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_t* self = container_of(mqtt_bridge_t, hsm, hsm);
  (void)data;

  switch(signal) {
    case signal_subscription_host_came_online:
      // The appliance host restarted — its ERD set may have changed, so clear
      // the local tracking set so that all ERDs are re-registered when new
      // subscription publications arrive.
      erd_set(self).clear();
      __attribute__((fallthrough));
    case tiny_hsm_signal_entry:
      // Intentionally fall through to the subscribe case below.
      // Do NOT clear erd_set on entry when the transition was triggered by
      // signal_mqtt_disconnected: the appliance hasn't changed, only the MQTT
      // broker connection was lost.  Clearing the set on every MQTT reconnect
      // causes every ERD to be "re-registered" (logged) as subscription
      // publications arrive, creating slow-looking logs (~2-3 s per ERD).
      __attribute__((fallthrough));
    case signal_subscription_failed:
    case signal_timer_expired:
      if(!tiny_gea3_erd_client_subscribe(self->erd_client, self->erd_host_address)) {
        arm_timer(self, resubscribe_delay);
      }
      break;

    case signal_subscription_added_or_retained:
      tiny_hsm_transition(hsm, state_subscribed);
      break;

    case tiny_hsm_signal_exit:
      disarm_timer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static void arm_periodic_timer(mqtt_bridge_t* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start_periodic(
    self->timer_group, &self->timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<mqtt_bridge_t*>(context)->hsm, signal_timer_expired, nullptr);
    });
}

static tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_t* self = container_of(mqtt_bridge_t, hsm, hsm);
  (void)data;

  switch(signal) {
    case tiny_hsm_signal_entry:
      arm_periodic_timer(self, subscription_retention_period);
      break;

    case signal_timer_expired:
      tiny_gea3_erd_client_retain_subscription(self->erd_client, self->erd_host_address);
      break;

    case signal_subscription_host_came_online:
    case signal_mqtt_disconnected:
      tiny_hsm_transition(hsm, state_subscribing);
      break;

    case tiny_hsm_signal_exit:
      disarm_timer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static const tiny_hsm_state_descriptor_t sub_hsm_state_descriptors[] = {
  { .state = sub_state_top, .parent = nullptr },
  { .state = state_subscribing, .parent = sub_state_top },
  { .state = state_subscribed, .parent = sub_state_top }
};
static const tiny_hsm_configuration_t sub_hsm_configuration = {
  .states = sub_hsm_state_descriptors,
  .state_count = element_count(sub_hsm_state_descriptors)
};

void mqtt_bridge_init(
  mqtt_bridge_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint8_t address)
{
  self->timer_group = timer_group;
  self->erd_client = erd_client;
  self->mqtt_client = mqtt_client;
  self->erd_host_address = address;
  self->erd_set = reinterpret_cast<void*>(new set<tiny_erd_t>());

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<mqtt_bridge_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      if(args->address != self->erd_host_address) {
        return;
      }

      switch(args->type) {
        case tiny_gea3_erd_client_activity_type_subscription_added_or_retained:
          tiny_hsm_send_signal(&self->hsm, signal_subscription_added_or_retained, nullptr);
          break;

        case tiny_gea3_erd_client_activity_type_subscription_publication_received:
          tiny_hsm_send_signal(&self->hsm, signal_subscription_publication_received, args);
          break;

        case tiny_gea3_erd_client_activity_type_subscription_host_came_online:
          tiny_hsm_send_signal(&self->hsm, signal_subscription_host_came_online, nullptr);
          break;

        case tiny_gea3_erd_client_activity_type_subscribe_failed:
          tiny_hsm_send_signal(&self->hsm, signal_subscription_failed, nullptr);
          break;

        case tiny_gea3_erd_client_activity_type_write_completed:
        case tiny_gea3_erd_client_activity_type_write_failed:
          handle_write_result(self->mqtt_client, args);
          break;
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  setup_write_request_subscription(self, mqtt_client);
  setup_disconnect_subscription(self, mqtt_client);

  tiny_hsm_init(&self->hsm, &sub_hsm_configuration, state_subscribing);
}

void mqtt_bridge_destroy(mqtt_bridge_t* self)
{
  // Guard against destroy() being called on a never-initialized struct (e.g.
  // in test teardowns that always call both bridge and polling destroy).
  if (!self->timer_group) {
    return;
  }

  // Stop the resubscribe timer so it cannot fire after the bridge is torn down.
  // tiny_timer_stop() is idempotent: safe to call even if the timer is not active.
  tiny_timer_stop(self->timer_group, &self->timer);

  // Remove all event subscriptions before freeing heap state.
  //
  // mqtt_bridge_init() subscribes three event callbacks that reference this
  // struct: erd_client_activity_subscription, mqtt_write_request_subscription,
  // and mqtt_disconnect_subscription.  If these remain registered after
  // destroy(), any subsequent event fires the HSM which dereferences
  // self->erd_set (freed below) — a use-after-free that corrupts the heap.
  tiny_event_unsubscribe(
    tiny_gea3_erd_client_on_activity(self->erd_client),
    &self->erd_client_activity_subscription);
  tiny_event_unsubscribe(
    mqtt_client_on_write_request(self->mqtt_client),
    &self->mqtt_write_request_subscription);
  tiny_event_unsubscribe(
    mqtt_client_on_mqtt_disconnect(self->mqtt_client),
    &self->mqtt_disconnect_subscription);

  delete reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
  self->erd_set = nullptr;
}

