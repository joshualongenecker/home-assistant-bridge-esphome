/*!
 * @file
 * @brief MQTT bridge implementations: subscription mode and polling mode.
 */

extern "C" {
#include "mqtt_bridge.h"
#include "mqtt_bridge_polling.h"
#include "tiny_utils.h"
#include "tiny_gea_constants.h"
}

#include "erd_lists.h"
#include <cstring>
#include <map>
#include <set>
#include <vector>

using namespace std;

// ============================================================================
// Shared constants and signals
// ============================================================================

enum {
  resubscribe_delay = 1000,
  subscription_retention_period = 30 * 1000,
  retry_delay = 100,
  appliance_lost_timeout = 60000,
  max_polling_retries = 3
};

enum {
  signal_start = tiny_hsm_signal_user_start,
  signal_timer_expired,
  signal_polling_timer_expired,
  signal_subscription_failed,
  signal_subscription_added_or_retained,
  signal_subscription_host_came_online,
  signal_subscription_publication_received,
  signal_read_failed,
  signal_read_completed,
  signal_mqtt_disconnected,
  signal_appliance_lost,
  signal_write_requested
};

// ============================================================================
// Shared utility templates
// ============================================================================

template<typename T>
static void arm_timer(T* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start(
    self->timer_group, &self->timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<T*>(context)->hsm, signal_timer_expired, nullptr);
    });
}

template<typename T>
static void disarm_timer(T* self)
{
  tiny_timer_stop(self->timer_group, &self->timer);
}

template<typename T>
static set<tiny_erd_t>& erd_set(T* self)
{
  return *reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
}

static void handle_write_result(
  i_mqtt_client_t* mqtt_client,
  const tiny_gea3_erd_client_on_activity_args_t* args)
{
  if(args->type == tiny_gea3_erd_client_activity_type_write_completed) {
    mqtt_client_update_erd_write_result(mqtt_client, args->write_completed.erd, true, 0);
  }
  else if(args->type == tiny_gea3_erd_client_activity_type_write_failed) {
    mqtt_client_update_erd_write_result(mqtt_client, args->write_failed.erd, false, args->write_failed.reason);
  }
}

template<typename T>
static void setup_write_request_subscription(T* self, i_mqtt_client_t* mqtt_client)
{
  tiny_event_subscription_init(
    &self->mqtt_write_request_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<T*>(context);
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(_args);
      tiny_hsm_send_signal(&self->hsm, signal_write_requested, args);
    });
  tiny_event_subscribe(mqtt_client_on_write_request(mqtt_client), &self->mqtt_write_request_subscription);
}

template<typename T>
static void setup_disconnect_subscription(T* self, i_mqtt_client_t* mqtt_client)
{
  tiny_event_subscription_init(
    &self->mqtt_disconnect_subscription, self, +[](void* context, const void*) {
      auto self = reinterpret_cast<T*>(context);
      erd_set(self).clear();
      tiny_hsm_send_signal(&self->hsm, signal_mqtt_disconnected, nullptr);
    });
  tiny_event_subscribe(mqtt_client_on_mqtt_disconnect(mqtt_client), &self->mqtt_disconnect_subscription);
}

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
    case tiny_hsm_signal_entry:
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
  (void)self;

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
  if(self->erd_client) {
    tiny_event_unsubscribe(tiny_gea3_erd_client_on_activity(self->erd_client), &self->erd_client_activity_subscription);
  }
  if(self->mqtt_client) {
    tiny_event_unsubscribe(mqtt_client_on_write_request(self->mqtt_client), &self->mqtt_write_request_subscription);
    tiny_event_unsubscribe(mqtt_client_on_mqtt_disconnect(self->mqtt_client), &self->mqtt_disconnect_subscription);
  }
  delete reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
}

// ============================================================================
// Polling bridge
// ============================================================================

static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_identify_appliance(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_appliance_api_feature_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

static map<tiny_erd_t, vector<uint8_t>>& erd_cache(mqtt_bridge_polling_t* self)
{
  return *reinterpret_cast<map<tiny_erd_t, vector<uint8_t>>*>(self->erd_cache);
}

static void arm_polling_timer(mqtt_bridge_polling_t* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start(
    self->timer_group, &self->polling_timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<mqtt_bridge_polling_t*>(context)->hsm, signal_polling_timer_expired, nullptr);
    });
}

static void reset_lost_appliance_timer(mqtt_bridge_polling_t* self)
{
  tiny_timer_stop(self->timer_group, &self->appliance_lost_timer);
  tiny_timer_start(
    self->timer_group, &self->appliance_lost_timer, appliance_lost_timeout, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<mqtt_bridge_polling_t*>(context)->hsm, signal_appliance_lost, nullptr);
    });
}

static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  switch(signal) {
    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);
      tiny_gea3_erd_client_write(self->erd_client, &self->request_id, self->erd_host_address, args->erd, args->value, args->size);
    } break;

    case signal_appliance_lost:
      tiny_hsm_transition(hsm, state_identify_appliance);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_identify_appliance(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch(signal) {
    case tiny_hsm_signal_entry:
      self->erd_host_address = tiny_gea_broadcast_address;
      self->polling_list_complete = false;
      self->current_state_name = "identify_appliance";
      __attribute__((fallthrough));

    case signal_timer_expired:
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, 0x0008);
      arm_timer(self, retry_delay);
      break;

    case signal_read_completed:
      // Ignore reads for ERDs other than the appliance type ERD (0x0008); they
      // are from concurrent activity on the shared bus and must not trigger a
      // premature transition out of identification with erd_host_address still
      // set to the broadcast address (0xFF).
      if(args->read_completed.erd != 0x0008) {
        break;
      }
      disarm_timer(self);
      reset_lost_appliance_timer(self);
      if(args->read_completed.data_size >= 1) {
        self->erd_host_address = args->address;
        self->appliance_type = *reinterpret_cast<const uint8_t*>(args->read_completed.data);
      }
      // If an API-parsed ERD list is available, run the appliance API feature ERD state
      // (mandatory for all appliances) and then go directly to polling. Otherwise,
      // run the full discovery chain: common → energy → appliance_api_feature → appliance.
      if(self->api_parsed_list != nullptr) {
        tiny_hsm_transition(hsm, state_add_appliance_api_feature_erds);
      }
      else {
        tiny_hsm_transition(hsm, state_add_common_erds);
      }
      break;

    case tiny_hsm_signal_exit:
      disarm_timer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static bool send_next_read_request(mqtt_bridge_polling_t* self)
{
  reset_lost_appliance_timer(self);
  self->erd_index++;
  bool more_erds_to_try = (self->erd_index < self->appliance_erd_list_count);
  if(more_erds_to_try) {
    self->request_id++;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    arm_timer(self, retry_delay);
  }
  return more_erds_to_try;
}

static void add_erd_to_polling_list(mqtt_bridge_polling_t* self, tiny_erd_t erd)
{
  if(erd_set(self).find(erd) == erd_set(self).end()) {
    mqtt_client_register_erd(self->mqtt_client, erd);
    erd_set(self).insert(erd);
    if(self->polling_list_count < POLLING_LIST_MAX_SIZE) {
      self->erd_polling_list[self->polling_list_count] = erd;
      self->polling_list_count++;
    }
  }
}

static tiny_hsm_result_t handle_discovery_list_signals(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch(signal) {
    case signal_timer_expired:
      if(!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, self->next_discovery_state);
      }
      break;

    case signal_read_completed:
      disarm_timer(self);
      add_erd_to_polling_list(self, args->read_completed.erd);
      mqtt_client_update_erd(
        self->mqtt_client,
        args->read_completed.erd,
        args->read_completed.data,
        args->read_completed.data_size);

      if(!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, self->next_discovery_state);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  if(signal == tiny_hsm_signal_entry) {
    self->current_state_name = "add_common_erds";
    self->next_discovery_state = state_add_energy_erds;
    self->appliance_erd_list = commonErds;
    self->appliance_erd_list_count = commonErdCount;
    self->erd_index = 0;
    self->polling_list_count = 0;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    arm_timer(self, retry_delay);
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  if(signal == tiny_hsm_signal_entry) {
    self->current_state_name = "add_energy_erds";
    self->next_discovery_state = state_add_appliance_api_feature_erds;
    self->appliance_erd_list = energyErds;
    self->appliance_erd_list_count = energyErdCount;
    self->erd_index = 0;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    arm_timer(self, retry_delay);
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_appliance_api_feature_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  if(signal == tiny_hsm_signal_entry) {
    self->current_state_name = "add_appliance_api_feature_erds";
    // When reached via api_parsed_list path (from state_identify_appliance), go directly
    // to polling after discovering the mandatory feature bit ERDs. When reached via the
    // full discovery chain (from state_add_energy_erds), continue to appliance-specific ERDs.
    self->next_discovery_state = (self->api_parsed_list != nullptr) ? state_polling : state_add_appliance_erds;
    self->appliance_erd_list = applianceApiFeatureErds;
    self->appliance_erd_list_count = applianceApiFeatureErdCount;
    self->erd_index = 0;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    arm_timer(self, retry_delay);
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  if(signal == tiny_hsm_signal_entry) {
    if(self->appliance_type >= maximumApplianceType) {
      self->appliance_type = 0;
    }
    self->current_state_name = "add_appliance_erds";
    self->next_discovery_state = state_polling;
    self->appliance_erd_list = applianceTypeToErdGroupTranslation[self->appliance_type].erdList;
    self->appliance_erd_list_count = applianceTypeToErdGroupTranslation[self->appliance_type].erdCount;
    self->erd_index = 0;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    arm_timer(self, retry_delay);
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static void send_next_poll_read_request(mqtt_bridge_polling_t* self)
{
  if(self->erd_index < self->polling_list_count) {
    self->request_id++;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->erd_polling_list[self->erd_index]);
    self->erd_index++;
    arm_timer(self, retry_delay);
  }
}

static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch(signal) {
    case tiny_hsm_signal_entry:
      erd_cache(self).clear();
      // When using an API-parsed list, register all ERDs upfront since
      // the discovery states are skipped.
      if(self->api_parsed_list != nullptr) {
        for(uint16_t i = 0; i < self->api_parsed_list_count; i++) {
          add_erd_to_polling_list(self, self->api_parsed_list[i]);
        }
      }
      // Add user-configured custom ERDs to the polling list. This works for
      // both discovery mode (custom ERDs appended after discovered ERDs) and
      // api_parsed_list mode (custom ERDs appended after API-parsed ERDs).
      if(self->custom_erd_list != nullptr) {
        for(uint16_t i = 0; i < self->custom_erd_list_count; i++) {
          add_erd_to_polling_list(self, self->custom_erd_list[i]);
        }
      }
      arm_polling_timer(self, self->polling_interval_ms);
      self->polling_list_complete = true;
      self->current_state_name = "polling";
      __attribute__((fallthrough));

    case signal_timer_expired:
      send_next_poll_read_request(self);
      break;

    case signal_polling_timer_expired:
      if((self->erd_index >= self->polling_list_count) || (self->polling_retries >= max_polling_retries)) {
        self->erd_index = 0;
        self->polling_retries = 0;
        send_next_poll_read_request(self);
      }
      else {
        self->polling_retries++;
      }
      arm_polling_timer(self, self->polling_interval_ms);
      break;

    case signal_read_completed: {
      disarm_timer(self);
      reset_lost_appliance_timer(self);
      tiny_erd_t erd = args->read_completed.erd;
      const uint8_t* erd_data = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t data_size = args->read_completed.data_size;
      // Register any ERD that arrives here for the first time. This handles
      // delayed discovery responses that arrive after the transition to polling
      // state (when the device takes longer than retry_delay to respond).
      add_erd_to_polling_list(self, erd);
      bool should_publish;
      if(self->only_publish_on_change) {
        auto& cache = erd_cache(self);
        auto it = cache.find(erd);
        bool data_changed;
        if(it == cache.end()) {
          data_changed = true;
        }
        else {
          data_changed = (it->second.size() != data_size) ||
            (memcmp(it->second.data(), erd_data, data_size) != 0);
        }
        if(data_changed) {
          cache[erd] = vector<uint8_t>(erd_data, erd_data + data_size);
        }
        should_publish = data_changed;
      }
      else {
        should_publish = true;
      }
      if(should_publish) {
        mqtt_client_update_erd(self->mqtt_client, erd, erd_data, data_size);
      }
      send_next_poll_read_request(self);
      break;
    }

    case signal_mqtt_disconnected:
      tiny_hsm_transition(&self->hsm, state_identify_appliance);
      break;

    case tiny_hsm_signal_exit:
      disarm_timer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static const tiny_hsm_state_descriptor_t poll_hsm_state_descriptors[] = {
  { .state = poll_state_top, .parent = nullptr },
  { .state = state_identify_appliance, .parent = poll_state_top },
  { .state = state_add_common_erds, .parent = poll_state_top },
  { .state = state_add_energy_erds, .parent = poll_state_top },
  { .state = state_add_appliance_api_feature_erds, .parent = poll_state_top },
  { .state = state_add_appliance_erds, .parent = poll_state_top },
  { .state = state_polling, .parent = poll_state_top }
};
static const tiny_hsm_configuration_t poll_hsm_configuration = {
  .states = poll_hsm_state_descriptors,
  .state_count = element_count(poll_hsm_state_descriptors)
};

void mqtt_bridge_polling_init(
  mqtt_bridge_polling_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint32_t polling_interval_ms,
  bool only_publish_on_change)
{
  self->timer_group = timer_group;
  self->erd_client = erd_client;
  self->mqtt_client = mqtt_client;
  self->polling_interval_ms = polling_interval_ms;
  self->only_publish_on_change = only_publish_on_change;
  // Initialized to nullptr/0; set by caller after initialization if needed.
  self->api_parsed_list = nullptr;
  self->api_parsed_list_count = 0;
  // Initialized to nullptr/0; set by caller after initialization if needed.
  self->custom_erd_list = nullptr;
  self->custom_erd_list_count = 0;
  self->erd_set = reinterpret_cast<void*>(new set<tiny_erd_t>());
  self->erd_cache = reinterpret_cast<void*>(new map<tiny_erd_t, vector<uint8_t>>());

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<mqtt_bridge_polling_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      switch(args->type) {
        case tiny_gea3_erd_client_activity_type_read_completed:
          tiny_hsm_send_signal(&self->hsm, signal_read_completed, args);
          break;

        case tiny_gea3_erd_client_activity_type_read_failed:
          tiny_hsm_send_signal(&self->hsm, signal_read_failed, args);
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

  tiny_hsm_init(&self->hsm, &poll_hsm_configuration, state_identify_appliance);
}

void mqtt_bridge_polling_destroy(mqtt_bridge_polling_t* self)
{
  if(self->erd_client) {
    tiny_event_unsubscribe(tiny_gea3_erd_client_on_activity(self->erd_client), &self->erd_client_activity_subscription);
  }
  if(self->mqtt_client) {
    tiny_event_unsubscribe(mqtt_client_on_write_request(self->mqtt_client), &self->mqtt_write_request_subscription);
    tiny_event_unsubscribe(mqtt_client_on_mqtt_disconnect(self->mqtt_client), &self->mqtt_disconnect_subscription);
  }
  delete reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
  delete reinterpret_cast<map<tiny_erd_t, vector<uint8_t>>*>(self->erd_cache);
}
