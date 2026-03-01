/*!
 * @file
 * @brief MQTT polling bridge implementation for GE Appliances integration
 */

extern "C" {
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

// GEA3 protocol constants
enum {
  erd_host_address = 0xC0,  // Default address for GE appliance host
  retry_delay = 100,         // Delay in ms before retrying read
  appliance_lost_timeout = 60000,  // 60 seconds timeout for appliance loss
  max_polling_retries = 3    // Maximum retries before restarting polling cycle
};

enum {
  signal_start = tiny_hsm_signal_user_start,
  signal_timer_expired,
  signal_polling_timer_expired,
  signal_read_failed,
  signal_read_completed,
  signal_mqtt_disconnected,
  signal_appliance_lost,
  signal_write_requested
};

// Common ERDs that most appliances support
static const tiny_erd_t common_erds[] = {
  0x0001, 0x0002, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009,
  0x000a, 0x000e, 0x0030, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036,
  0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e,
  0x003f, 0x004e, 0x004f, 0x0050, 0x0051, 0x0052
};
static const uint16_t common_erd_count = sizeof(common_erds) / sizeof(common_erds[0]);

static void arm_timer(mqtt_bridge_polling_t* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start(
    self->timer_group, &self->timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<mqtt_bridge_polling_t*>(context)->hsm, signal_timer_expired, nullptr);
    });
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

static void disarm_timer(mqtt_bridge_polling_t* self)
{
  tiny_timer_stop(self->timer_group, &self->timer);
}

static set<tiny_erd_t>& erd_set(mqtt_bridge_polling_t* self)
{
  return *reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
}

static map<tiny_erd_t, vector<uint8_t>>& erd_cache(mqtt_bridge_polling_t* self)
{
  return *reinterpret_cast<map<tiny_erd_t, vector<uint8_t>>*>(self->erd_cache);
}

static tiny_hsm_result_t state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_identify_appliance(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

static tiny_hsm_result_t state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  switch(signal) {
    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);
      tiny_gea3_erd_client_write(self->erd_client, &self->request_id, self->erd_host_address, args->erd, args->value, args->size);
    } break;

    case signal_appliance_lost: {
      tiny_hsm_transition(hsm, state_identify_appliance);
    } break;

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
    case tiny_hsm_signal_entry: {
      self->erd_host_address = tiny_gea_broadcast_address;
    }
      __attribute__((fallthrough));

    case signal_timer_expired: {
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, 0x0008);
      arm_timer(self, retry_delay);
      break;
    }

    case signal_read_completed: {
      disarm_timer(self);
      reset_lost_appliance_timer(self);
      if(args->read_completed.erd == 0x0008) {
        self->erd_host_address = args->address;
      }

      const uint8_t* appliance_type_response = (const uint8_t*)args->read_completed.data;
      self->appliance_type = *appliance_type_response;
      tiny_hsm_transition(hsm, state_add_common_erds);
      break;
    }

    case tiny_hsm_signal_exit: {
      disarm_timer(self);
      break;
    }

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
    
    // Only add to polling list if not already present and there's space
    if (self->polling_list_count < POLLING_LIST_MAX_SIZE) {
      self->erd_polling_list[self->polling_list_count] = erd;
      self->polling_list_count++;
    }
  }
}

static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch(signal) {
    case tiny_hsm_signal_entry:
      self->appliance_erd_list = common_erds;
      self->appliance_erd_list_count = common_erd_count;
      self->erd_index = 0;
      self->polling_list_count = 0;
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
      arm_timer(self, retry_delay);
      break;

    case signal_timer_expired:
      if(!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, state_add_energy_erds);
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
        tiny_hsm_transition(hsm, state_add_energy_erds);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch(signal) {
    case tiny_hsm_signal_entry:
      self->appliance_erd_list = energyErds;
      self->appliance_erd_list_count = energyErdCount;
      self->erd_index = 0;

      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
      arm_timer(self, retry_delay);
      break;

    case signal_timer_expired:
      if(!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, state_add_appliance_erds);
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
        tiny_hsm_transition(hsm, state_add_appliance_erds);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch(signal) {
    case tiny_hsm_signal_entry:
      // Validate appliance type and select appropriate ERD list
      if (self->appliance_type >= maximumApplianceType) {
        self->appliance_type = 0;  // Default to first entry if out of range
      }
      
      self->appliance_erd_list = applianceTypeToErdGroupTranslation[self->appliance_type].erdList;
      self->appliance_erd_list_count = applianceTypeToErdGroupTranslation[self->appliance_type].erdCount;
      self->erd_index = 0;

      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
      arm_timer(self, retry_delay);
      break;

    case signal_timer_expired:
      if(!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, state_polling);
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
        tiny_hsm_transition(hsm, state_polling);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
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
      arm_polling_timer(self, self->polling_interval_ms);
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

    case signal_read_completed:
      disarm_timer(self);
      reset_lost_appliance_timer(self);
      {
        tiny_erd_t erd = args->read_completed.erd;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(args->read_completed.data);
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
              (memcmp(it->second.data(), data, data_size) != 0);
          }
          if(data_changed) {
            cache[erd] = vector<uint8_t>(data, data + data_size);
          }
          should_publish = data_changed;
        }
        else {
          should_publish = true;
        }
        if(should_publish) {
          mqtt_client_update_erd(self->mqtt_client, erd, data, data_size);
        }
      }

      send_next_poll_read_request(self);
      break;

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

static const tiny_hsm_state_descriptor_t hsm_state_descriptors[] = {
  { .state = state_top, .parent = nullptr },
  { .state = state_identify_appliance, .parent = state_top },
  { .state = state_add_common_erds, .parent = state_top },
  { .state = state_add_energy_erds, .parent = state_top },
  { .state = state_add_appliance_erds, .parent = state_top },
  { .state = state_polling, .parent = state_top }
};

static const tiny_hsm_configuration_t hsm_configuration = {
  .states = hsm_state_descriptors,
  .state_count = element_count(hsm_state_descriptors)
};

void mqtt_bridge_polling_init(
  mqtt_bridge_polling_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint32_t polling_interval_ms,
  bool only_publish_on_change,
  uint8_t initial_address,
  uint8_t initial_appliance_type)
{
  self->timer_group = timer_group;
  self->erd_client = erd_client;
  self->mqtt_client = mqtt_client;
  self->polling_interval_ms = polling_interval_ms;
  self->only_publish_on_change = only_publish_on_change;
  self->erd_host_address = initial_address;
  self->appliance_type = initial_appliance_type;
  self->erd_set = reinterpret_cast<void*>(new set<tiny_erd_t>());
  self->erd_cache = reinterpret_cast<void*>(new map<tiny_erd_t, vector<uint8_t>>());

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<mqtt_bridge_polling_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      // Only process events from this bridge's board address.
      // Allow broadcast address (0xFF) through during state_identify_appliance.
      if(self->erd_host_address != tiny_gea_broadcast_address &&
         args->address != self->erd_host_address) {
        return;
      }

      switch(args->type) {
        case tiny_gea3_erd_client_activity_type_read_completed:
          tiny_hsm_send_signal(&self->hsm, signal_read_completed, args);
          break;

        case tiny_gea3_erd_client_activity_type_read_failed:
          tiny_hsm_send_signal(&self->hsm, signal_read_failed, args);
          break;

        case tiny_gea3_erd_client_activity_type_write_completed:
          mqtt_client_update_erd_write_result(self->mqtt_client, args->write_completed.erd, true, 0);
          break;

        case tiny_gea3_erd_client_activity_type_write_failed:
          mqtt_client_update_erd_write_result(self->mqtt_client, args->write_failed.erd, false, args->write_failed.reason);
          break;
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  tiny_event_subscription_init(
    &self->mqtt_write_request_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<mqtt_bridge_polling_t*>(context);
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(_args);
      tiny_hsm_send_signal(&self->hsm, signal_write_requested, args);
    });
  tiny_event_subscribe(mqtt_client_on_write_request(mqtt_client), &self->mqtt_write_request_subscription);

  tiny_event_subscription_init(
    &self->mqtt_disconnect_subscription, self, +[](void* context, const void*) {
      auto self = reinterpret_cast<mqtt_bridge_polling_t*>(context);
      reinterpret_cast<set<tiny_erd_t>*>(self->erd_set)->clear();
      tiny_hsm_send_signal(&self->hsm, signal_mqtt_disconnected, nullptr);
    });
  tiny_event_subscribe(mqtt_client_on_mqtt_disconnect(mqtt_client), &self->mqtt_disconnect_subscription);

  if(initial_address == tiny_gea_broadcast_address) {
    tiny_hsm_init(&self->hsm, &hsm_configuration, state_identify_appliance);
  }
  else {
    tiny_hsm_init(&self->hsm, &hsm_configuration, state_add_common_erds);
  }
}

void mqtt_bridge_polling_destroy(mqtt_bridge_polling_t* self)
{
  delete reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
  delete reinterpret_cast<map<tiny_erd_t, vector<uint8_t>>*>(self->erd_cache);
}
