/*!
 * @file
 * @brief GEA2 MQTT Bridge Implementation
 */

#include "gea2_mqtt_bridge.h"
#include "gea2_appliance_erds.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

extern "C" {
#include "i_tiny_gea2_erd_client.h"
#include "tiny_gea_constants.h"
#include "tiny_utils.h"
}

namespace esphome {
namespace geappliances_bridge {

static const char *const TAG = "gea2_mqtt_bridge";

enum {
  retry_delay = 3000,
  appliance_lost_timeout = 60000,
  mqtt_info_update_period = 1000,
  ticks_per_second = 1000
};

enum {
  signal_start = tiny_hsm_signal_user_start,
  signal_timer_expired,
  signal_read_failed,
  signal_read_completed,
  signal_mqtt_disconnected,
  signal_appliance_lost,
  signal_write_requested
};

Gea2MqttBridge::Gea2MqttBridge()
    : uptime_(0),
      last_erd_polled_successfully_(0),
      polling_list_count_(0),
      timer_group_(nullptr),
      erd_client_(nullptr),
      mqtt_client_(nullptr),
      erd_set_(nullptr),
      request_id_(0),
      erd_host_address_(0xFF),
      appliance_type_(0),
      appliance_erd_list_(nullptr),
      appliance_erd_list_count_(0),
      erd_index_(0) {
  erd_set_ = new std::set<tiny_erd_t>();
}

Gea2MqttBridge::~Gea2MqttBridge() {
  if (erd_set_) {
    delete erd_set_;
    erd_set_ = nullptr;
  }
}

bool Gea2MqttBridge::valid_polling_list_loaded() {
  polling_list_count_ = 0;
  
  // Try to load from NVS
  pref_ = global_preferences->make_preference<NVSData>(fnv1_hash("gea2_poll"));
  
  NVSData data;
  if (pref_.load(&data)) {
    ESP_LOGI(TAG, "NV storage found and loaded");
    polling_list_count_ = data.polling_list_count;
    ESP_LOGI(TAG, "Stored number of polled ERDs is %d", polling_list_count_);
    
    if (polling_list_count_ > 0 && polling_list_count_ <= GEA2_POLLING_LIST_MAX_SIZE) {
      memcpy(erd_polling_list_, data.erd_polling_list, sizeof(erd_polling_list_));
      erd_host_address_ = data.erd_host_address;
      ESP_LOGI(TAG, "GEA address set to 0x%02X", erd_host_address_);
      return true;
    }
  }
  
  ESP_LOGI(TAG, "No valid polling list found in NV storage");
  return false;
}

void Gea2MqttBridge::save_polling_list_to_nv_store() {
  NVSData data;
  data.polling_list_count = polling_list_count_;
  data.erd_host_address = erd_host_address_;
  memcpy(data.erd_polling_list, erd_polling_list_, sizeof(erd_polling_list_));
  
  if (pref_.save(&data)) {
    ESP_LOGI(TAG, "Saved polling list to NV storage: %d ERDs, address 0x%02X",
             polling_list_count_, erd_host_address_);
  } else {
    ESP_LOGW(TAG, "Failed to save polling list to NV storage");
  }
}

void Gea2MqttBridge::clear_nv_storage() {
  NVSData data = {};
  pref_.save(&data);
  ESP_LOGI(TAG, "Cleared NV storage");
}

void Gea2MqttBridge::publish_mqtt_info() {
  uptime_ += (mqtt_info_update_period / ticks_per_second);
  
  char uptime_str[32];
  snprintf(uptime_str, sizeof(uptime_str), "%lu", uptime_);
  mqtt_client_publish_sub_topic(mqtt_client_, "gea2/uptime", uptime_str);
  
  char last_erd_str[16];
  snprintf(last_erd_str, sizeof(last_erd_str), "0x%04X", last_erd_polled_successfully_);
  mqtt_client_publish_sub_topic(mqtt_client_, "gea2/lastErd", last_erd_str);
}

void Gea2MqttBridge::start_mqtt_info_timer() {
  uptime_ = 0;
  tiny_timer_start_periodic(timer_group_, &mqtt_information_timer_, mqtt_info_update_period, this,
    +[](void* context) {
      reinterpret_cast<Gea2MqttBridge*>(context)->publish_mqtt_info();
    });
}

void Gea2MqttBridge::stop_mqtt_info_timer() {
  uptime_ = 0xFFFFFFFF;
  tiny_timer_stop(timer_group_, &mqtt_information_timer_);
}

void Gea2MqttBridge::arm_timer(tiny_timer_ticks_t ticks) {
  tiny_timer_start(timer_group_, &timer_, ticks, this,
    +[](void* context) {
      auto self = reinterpret_cast<Gea2MqttBridge*>(context);
      tiny_hsm_send_signal(&self->hsm_, signal_timer_expired, nullptr);
    });
}

void Gea2MqttBridge::disarm_lost_appliance_timer() {
  tiny_timer_stop(timer_group_, &appliance_lost_timer_);
}

void Gea2MqttBridge::reset_lost_appliance_timer() {
  tiny_timer_start(timer_group_, &appliance_lost_timer_, appliance_lost_timeout, this,
    +[](void* context) {
      auto self = reinterpret_cast<Gea2MqttBridge*>(context);
      tiny_hsm_send_signal(&self->hsm_, signal_appliance_lost, nullptr);
    });
}

void Gea2MqttBridge::disarm_retry_timer() {
  tiny_timer_stop(timer_group_, &timer_);
}

bool Gea2MqttBridge::send_next_read_request() {
  erd_index_++;
  bool more_erds_to_try = (erd_index_ < appliance_erd_list_count_);
  if (more_erds_to_try) {
    request_id_++;
    tiny_gea2_erd_client_read(erd_client_, &request_id_, erd_host_address_, 
                               appliance_erd_list_[erd_index_]);
    arm_timer(retry_delay);
  }
  return more_erds_to_try;
}

void Gea2MqttBridge::add_erd_to_polling_list(tiny_erd_t erd) {
  if (erd_set_->find(erd) == erd_set_->end()) {
    mqtt_client_register_erd(mqtt_client_, erd);
    erd_set_->insert(erd);
  }
  
  if (polling_list_count_ < GEA2_POLLING_LIST_MAX_SIZE) {
    erd_polling_list_[polling_list_count_] = erd;
    polling_list_count_++;
    ESP_LOGD(TAG, "#%d Add ERD 0x%04X to polling list", polling_list_count_, erd);
  } else {
    ESP_LOGW(TAG, "Polling list full, cannot add ERD 0x%04X", erd);
  }
}

void Gea2MqttBridge::send_next_poll_read_request() {
  erd_index_++;
  if (erd_index_ >= polling_list_count_) {
    erd_index_ = 0;
  }
  request_id_++;
  tiny_gea2_erd_client_read(erd_client_, &request_id_, erd_host_address_,
                             erd_polling_list_[erd_index_]);
  arm_timer(retry_delay);
}

// State machine implementations

tiny_hsm_result_t Gea2MqttBridge::state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data) {
  auto self = reinterpret_cast<Gea2MqttBridge*>(container_of(Gea2MqttBridge, hsm_, hsm));
  
  switch(signal) {
    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);
      tiny_gea2_erd_client_write(self->erd_client_, &self->request_id_, self->erd_host_address_,
                                  args->erd, args->value, args->size);
      break;
    }
    
    case signal_appliance_lost: {
      self->clear_nv_storage();
      tiny_hsm_transition(hsm, state_identify_appliance);
      break;
    }
    
    default:
      return tiny_hsm_result_signal_deferred;
  }
  
  return tiny_hsm_result_signal_consumed;
}

tiny_hsm_result_t Gea2MqttBridge::state_identify_appliance(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data) {
  auto self = reinterpret_cast<Gea2MqttBridge*>(container_of(Gea2MqttBridge, hsm_, hsm));
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(data);
  
  switch(signal) {
    case tiny_hsm_signal_entry:
      self->erd_host_address_ = tiny_gea_broadcast_address;
      __attribute__((fallthrough));
      
    case signal_timer_expired:
      ESP_LOGD(TAG, "Asking for appliance type ERD 0x0008 from address 0x%02X", self->erd_host_address_);
      tiny_gea2_erd_client_read(self->erd_client_, &self->request_id_, self->erd_host_address_, 0x0008);
      self->arm_timer(retry_delay);
      break;
      
    case signal_read_completed:
      self->disarm_retry_timer();
      self->disarm_lost_appliance_timer();
      if (args->read_completed.erd == 0x0008) {
        self->erd_host_address_ = args->address;
        ESP_LOGI(TAG, "Using GEA address 0x%02X", self->erd_host_address_);
      }
      
      self->appliance_type_ = reinterpret_cast<const uint8_t*>(args->read_completed.data)[0];
      ESP_LOGI(TAG, "Appliance type: 0x%02X", self->appliance_type_);
      tiny_hsm_transition(hsm, state_add_common_erds);
      break;
      
    case tiny_hsm_signal_exit:
      self->disarm_retry_timer();
      break;
      
    default:
      return tiny_hsm_result_signal_deferred;
  }
  
  return tiny_hsm_result_signal_consumed;
}

tiny_hsm_result_t Gea2MqttBridge::state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data) {
  auto self = reinterpret_cast<Gea2MqttBridge*>(container_of(Gea2MqttBridge, hsm_, hsm));
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(data);
  
  switch(signal) {
    case tiny_hsm_signal_entry: {
      const gea2_erd_list_t* common_erds = gea2_get_common_erd_list();
      self->appliance_erd_list_ = common_erds->erd_list;
      self->appliance_erd_list_count_ = common_erds->erd_count;
      ESP_LOGI(TAG, "Looking for %d common ERDs", self->appliance_erd_list_count_);
      self->erd_index_ = 0;
      self->polling_list_count_ = 0;
      tiny_gea2_erd_client_read(self->erd_client_, &self->request_id_, self->erd_host_address_,
                                 self->appliance_erd_list_[self->erd_index_]);
      self->arm_timer(retry_delay);
      break;
    }
    
    case signal_timer_expired:
      if (!self->send_next_read_request()) {
        tiny_hsm_transition(hsm, state_add_energy_erds);
      }
      break;
      
    case signal_read_completed:
      self->disarm_retry_timer();
      self->add_erd_to_polling_list(args->read_completed.erd);
      mqtt_client_update_erd(self->mqtt_client_, args->read_completed.erd,
                              args->read_completed.data, args->read_completed.data_size);
      
      if (!self->send_next_read_request()) {
        tiny_hsm_transition(hsm, state_add_energy_erds);
      }
      break;
      
    default:
      return tiny_hsm_result_signal_deferred;
  }
  
  return tiny_hsm_result_signal_consumed;
}

tiny_hsm_result_t Gea2MqttBridge::state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data) {
  auto self = reinterpret_cast<Gea2MqttBridge*>(container_of(Gea2MqttBridge, hsm_, hsm));
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(data);
  
  switch(signal) {
    case tiny_hsm_signal_entry: {
      const gea2_erd_list_t* energy_erds = gea2_get_energy_erd_list();
      self->appliance_erd_list_ = energy_erds->erd_list;
      self->appliance_erd_list_count_ = energy_erds->erd_count;
      ESP_LOGI(TAG, "Looking for %d energy ERDs", self->appliance_erd_list_count_);
      self->erd_index_ = 0;
      tiny_gea2_erd_client_read(self->erd_client_, &self->request_id_, self->erd_host_address_,
                                 self->appliance_erd_list_[self->erd_index_]);
      self->arm_timer(retry_delay);
      break;
    }
    
    case signal_timer_expired:
      if (!self->send_next_read_request()) {
        tiny_hsm_transition(hsm, state_add_appliance_erds);
      }
      break;
      
    case signal_read_completed:
      self->disarm_retry_timer();
      self->add_erd_to_polling_list(args->read_completed.erd);
      mqtt_client_update_erd(self->mqtt_client_, args->read_completed.erd,
                              args->read_completed.data, args->read_completed.data_size);
      
      if (!self->send_next_read_request()) {
        tiny_hsm_transition(hsm, state_add_appliance_erds);
      }
      break;
      
    default:
      return tiny_hsm_result_signal_deferred;
  }
  
  return tiny_hsm_result_signal_consumed;
}

tiny_hsm_result_t Gea2MqttBridge::state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data) {
  auto self = reinterpret_cast<Gea2MqttBridge*>(container_of(Gea2MqttBridge, hsm_, hsm));
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(data);
  
  switch(signal) {
    case tiny_hsm_signal_entry: {
      const gea2_erd_list_t* appliance_erds = gea2_get_appliance_erd_list(self->appliance_type_);
      self->appliance_erd_list_ = appliance_erds->erd_list;
      self->appliance_erd_list_count_ = appliance_erds->erd_count;
      ESP_LOGI(TAG, "Looking for %d appliance-specific ERDs", self->appliance_erd_list_count_);
      self->erd_index_ = 0;
      tiny_gea2_erd_client_read(self->erd_client_, &self->request_id_, self->erd_host_address_,
                                 self->appliance_erd_list_[self->erd_index_]);
      self->arm_timer(retry_delay);
      break;
    }
    
    case signal_timer_expired:
      if (!self->send_next_read_request()) {
        tiny_hsm_transition(hsm, state_poll_erds_from_list);
      }
      break;
      
    case signal_read_completed:
      self->disarm_retry_timer();
      self->add_erd_to_polling_list(args->read_completed.erd);
      mqtt_client_update_erd(self->mqtt_client_, args->read_completed.erd,
                              args->read_completed.data, args->read_completed.data_size);
      
      if (!self->send_next_read_request()) {
        tiny_hsm_transition(hsm, state_poll_erds_from_list);
      }
      break;
      
    default:
      return tiny_hsm_result_signal_deferred;
  }
  
  return tiny_hsm_result_signal_consumed;
}

tiny_hsm_result_t Gea2MqttBridge::state_poll_erds_from_list(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data) {
  auto self = reinterpret_cast<Gea2MqttBridge*>(container_of(Gea2MqttBridge, hsm_, hsm));
  auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(data);
  
  switch(signal) {
    case tiny_hsm_signal_entry:
      self->disarm_lost_appliance_timer();
      self->reset_lost_appliance_timer();
      self->save_polling_list_to_nv_store();
      ESP_LOGI(TAG, "Polling %d ERDs", self->polling_list_count_);
      __attribute__((fallthrough));
      
    case signal_timer_expired:
      self->send_next_poll_read_request();
      break;
      
    case signal_read_completed:
      self->disarm_retry_timer();
      self->disarm_lost_appliance_timer();
      self->reset_lost_appliance_timer();
      mqtt_client_update_erd(self->mqtt_client_, args->read_completed.erd,
                              args->read_completed.data, args->read_completed.data_size);
      self->last_erd_polled_successfully_ = args->read_completed.erd;
      self->send_next_poll_read_request();
      break;
      
    case signal_mqtt_disconnected:
      if (self->valid_polling_list_loaded()) {
        ESP_LOGI(TAG, "MQTT reconnect with previously discovered appliance");
        tiny_hsm_transition(hsm, state_poll_erds_from_list);
      } else {
        ESP_LOGI(TAG, "MQTT reconnect, identify new appliance");
        tiny_hsm_transition(hsm, state_identify_appliance);
      }
      break;
      
    case tiny_hsm_signal_exit:
      self->disarm_retry_timer();
      break;
      
    default:
      return tiny_hsm_result_signal_deferred;
  }
  
  return tiny_hsm_result_signal_consumed;
}

// Initialization and destroy

static const tiny_hsm_state_descriptor_t hsm_state_descriptors[] = {
  { .state = Gea2MqttBridge::state_top, .parent = nullptr },
  { .state = Gea2MqttBridge::state_identify_appliance, .parent = Gea2MqttBridge::state_top },
  { .state = Gea2MqttBridge::state_add_common_erds, .parent = Gea2MqttBridge::state_top },
  { .state = Gea2MqttBridge::state_add_energy_erds, .parent = Gea2MqttBridge::state_top },
  { .state = Gea2MqttBridge::state_add_appliance_erds, .parent = Gea2MqttBridge::state_top },
  { .state = Gea2MqttBridge::state_poll_erds_from_list, .parent = Gea2MqttBridge::state_top }
};

static const tiny_hsm_configuration_t hsm_configuration = {
  .states = hsm_state_descriptors,
  .state_count = sizeof(hsm_state_descriptors) / sizeof(hsm_state_descriptors[0])
};

void Gea2MqttBridge::init(
    tiny_timer_group_t* timer_group,
    i_tiny_gea2_erd_client_t* erd_client,
    i_mqtt_client_t* mqtt_client) {
  ESP_LOGI(TAG, "GEA2 Bridge init start");
  
  timer_group_ = timer_group;
  erd_client_ = erd_client;
  mqtt_client_ = mqtt_client;
  
  start_mqtt_info_timer();
  
  // Subscribe to ERD client activity
  tiny_event_subscription_init(&erd_client_activity_subscription_, this,
    +[](void* context, const void* _args) {
      auto self = reinterpret_cast<Gea2MqttBridge*>(context);
      auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(_args);
      
      switch(args->type) {
        case tiny_gea2_erd_client_activity_type_read_completed:
          tiny_hsm_send_signal(&self->hsm_, signal_read_completed, args);
          break;
          
        case tiny_gea2_erd_client_activity_type_read_failed:
          tiny_hsm_send_signal(&self->hsm_, signal_read_failed, args);
          break;
          
        case tiny_gea2_erd_client_activity_type_write_completed:
          mqtt_client_update_erd_write_result(self->mqtt_client_, args->write_completed.erd, true, 0);
          break;
          
        case tiny_gea2_erd_client_activity_type_write_failed:
          mqtt_client_update_erd_write_result(self->mqtt_client_, args->write_failed.erd, false, 
                                                args->write_failed.reason);
          break;
      }
    });
  tiny_event_subscribe(tiny_gea2_erd_client_on_activity(erd_client), &erd_client_activity_subscription_);
  
  // Subscribe to MQTT write requests
  tiny_event_subscription_init(&mqtt_write_request_subscription_, this,
    +[](void* context, const void* _args) {
      auto self = reinterpret_cast<Gea2MqttBridge*>(context);
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(_args);
      tiny_hsm_send_signal(&self->hsm_, signal_write_requested, args);
    });
  tiny_event_subscribe(mqtt_client_on_write_request(mqtt_client), &mqtt_write_request_subscription_);
  
  // Subscribe to MQTT disconnect
  tiny_event_subscription_init(&mqtt_disconnect_subscription_, this,
    +[](void* context, const void*) {
      auto self = reinterpret_cast<Gea2MqttBridge*>(context);
      self->erd_set_->clear();
      tiny_hsm_send_signal(&self->hsm_, signal_mqtt_disconnected, nullptr);
    });
  tiny_event_subscribe(mqtt_client_on_mqtt_disconnect(mqtt_client), &mqtt_disconnect_subscription_);
  
  // Initialize state machine
  if (valid_polling_list_loaded()) {
    ESP_LOGI(TAG, "Start HSM with previously discovered appliance");
    tiny_hsm_init(&hsm_, &hsm_configuration, state_poll_erds_from_list);
  } else {
    ESP_LOGI(TAG, "Start HSM and identify new appliance");
    tiny_hsm_init(&hsm_, &hsm_configuration, state_identify_appliance);
  }
  
  ESP_LOGI(TAG, "GEA2 Bridge init done");
}

void Gea2MqttBridge::destroy() {
  ESP_LOGI(TAG, "GEA2 Bridge destroy start");
  stop_mqtt_info_timer();
  ESP_LOGI(TAG, "GEA2 Bridge destroy done");
}

}  // namespace geappliances_bridge
}  // namespace esphome
