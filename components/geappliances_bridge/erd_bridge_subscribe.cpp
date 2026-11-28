#include "erd_bridge_subscribe.h"
#include "erd_cache.h"
#include "geappliances_bridge_log.h"
#include "erd_registry.h"
#include "esphome/core/log.h"

GEA_TAG(TAG) = "erd_bridge_subscribe";

static tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_steady(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_failed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

static tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_subscribe_t* self = container_of(erd_bridge_subscribe_t, hsm, hsm);

  switch(signal) {
    case signal_subscription_publication_received: {
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      auto erd = args->subscription_publication_received.erd;

      if (self->erd_registry && !self->erd_registry->is_valid(erd)) {
        break;
      }

      bool is_new = !erd_set_contains(&self->erd_set, erd);
      if (is_new) {
        erd_set_insert(&self->erd_set, erd);
        tiny_hsm_transition(hsm, state_subscribed);
      }

      erd_cache_update(self->erd_cache, erd, self->erd_host_address,
              reinterpret_cast<const uint8_t*>(args->subscription_publication_received.data),
              args->subscription_publication_received.data_size);
    } break;

    case signal_quiet_period_expired:
      if (self->erd_set.count == 0) {
        tiny_hsm_transition(hsm, state_failed);
      } else {
        tiny_hsm_transition(hsm, state_steady);
      }
      break;

    case signal_timer_expired:
      tiny_gea3_erd_client_retain_subscription(self->erd_client, self->erd_host_address);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_subscribing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_subscribe_t* self = container_of(erd_bridge_subscribe_t, hsm, hsm);
  (void)data;

  switch(signal) {
    case signal_subscription_host_came_online:
      erd_set_clear(&self->erd_set);
      __attribute__((fallthrough));
    case tiny_hsm_signal_entry:
      disarm_timer(self);
      self->current_state = subscription_state_subscribing;
      __attribute__((fallthrough));
    case signal_subscription_failed:
      self->subscribe_failure_count++;
      if (self->subscribe_failure_count >= 3) {
        tiny_hsm_transition(hsm, state_failed);
        break;
      }
      __attribute__((fallthrough));
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

static void arm_periodic_timer(erd_bridge_subscribe_t* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start_periodic(
    self->timer_group, &self->timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<erd_bridge_subscribe_t*>(context)->hsm, signal_timer_expired, nullptr);
    });
}

static void arm_quiet_timer(erd_bridge_subscribe_t* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_stop(self->timer_group, &self->quiet_timer);
  tiny_timer_start(
    self->timer_group, &self->quiet_timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<erd_bridge_subscribe_t*>(context)->hsm, signal_quiet_period_expired, nullptr);
    });
}

static tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_subscribe_t* self = container_of(erd_bridge_subscribe_t, hsm, hsm);
  (void)data;

  switch(signal) {
    case tiny_hsm_signal_entry:
      self->current_state = subscription_state_subscribed;
      self->subscribe_failure_count = 0;
      arm_periodic_timer(self, subscription_retention_period);
      arm_quiet_timer(self, subscription_quiet_period);
      break;

    case signal_subscription_host_came_online:
      tiny_hsm_transition(hsm, state_subscribing);
      break;

    case tiny_hsm_signal_exit:
      tiny_timer_stop(self->timer_group, &self->quiet_timer);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_steady(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_subscribe_t* self = container_of(erd_bridge_subscribe_t, hsm, hsm);
  (void)data;

  switch(signal) {
    case tiny_hsm_signal_entry:
      self->current_state = subscription_state_steady;
      tiny_timer_stop(self->timer_group, &self->quiet_timer);
      break;

    case signal_subscription_host_came_online:
      tiny_hsm_transition(hsm, state_subscribing);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_failed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_subscribe_t* self = container_of(erd_bridge_subscribe_t, hsm, hsm);
  (void)data;

  switch(signal) {
    case tiny_hsm_signal_entry:
      self->current_state = subscription_state_failed;
      disarm_timer(self);
      tiny_timer_stop(self->timer_group, &self->quiet_timer);
      ESP_LOGI(TAG, "Subscription not supported by appliance, falling back to polling");
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static const tiny_hsm_state_descriptor_t sub_hsm_state_descriptors[] = {
  { .state = sub_state_top, .parent = nullptr },
  { .state = state_subscribing, .parent = sub_state_top },
  { .state = state_subscribed, .parent = sub_state_top },
  { .state = state_steady, .parent = sub_state_top },
  { .state = state_failed, .parent = sub_state_top }
};
static const tiny_hsm_configuration_t sub_hsm_configuration = {
  .states = sub_hsm_state_descriptors,
  .state_count = element_count(sub_hsm_state_descriptors)
};

void erd_bridge_subscribe_init(
  erd_bridge_subscribe_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint8_t address,
  erd_cache_t* cache)
{
  self->timer_group = timer_group;
  self->erd_client = erd_client;
  self->erd_host_address = address;
  self->erd_cache = cache;
  self->erd_registry = nullptr;
  self->current_state = subscription_state_none;
  self->subscribe_failure_count = 0;
  erd_set_init(&self->erd_set);

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<erd_bridge_subscribe_t*>(context);
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
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  tiny_hsm_init(&self->hsm, &sub_hsm_configuration, state_subscribing);
}

void erd_bridge_subscribe_destroy(erd_bridge_subscribe_t* self)
{
  if (!self->timer_group) {
    return;
  }

  tiny_timer_stop(self->timer_group, &self->timer);
  tiny_timer_stop(self->timer_group, &self->quiet_timer);

  if (self->erd_client != nullptr) {
    tiny_event_unsubscribe(
      tiny_gea3_erd_client_on_activity(self->erd_client),
      &self->erd_client_activity_subscription);
  }

  self->current_state = subscription_state_none;
}

void erd_bridge_subscribe_set_erd_registry(
  erd_bridge_subscribe_t* self,
  esphome::geappliances_bridge::ErdRegistry* erd_registry)
{
  self->erd_registry = erd_registry;
}
