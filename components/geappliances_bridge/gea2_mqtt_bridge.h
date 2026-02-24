/*!
 * @file
 * @brief GEA2 MQTT Bridge - Polls ERDs and publishes to MQTT
 * 
 * GEA2 is an older protocol that only supports polling (no subscribe).
 * This bridge discovers available ERDs by polling, stores them in NVS,
 * and continuously polls the discovered ERDs.
 */

#pragma once

#include "esphome/core/preferences.h"
#include <set>

extern "C" {
#include "i_mqtt_client.h"
#include "i_tiny_gea2_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"
}

namespace esphome {
namespace geappliances_bridge {

#define GEA2_POLLING_LIST_MAX_SIZE 256

class Gea2MqttBridge {
 public:
  Gea2MqttBridge();
  ~Gea2MqttBridge();

  void init(
    tiny_timer_group_t* timer_group,
    i_tiny_gea2_erd_client_t* erd_client,
    i_mqtt_client_t* mqtt_client);

  void destroy();

  // State machine states (public for use in hsm_state_descriptors[])
  static tiny_hsm_result_t state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  static tiny_hsm_result_t state_identify_appliance(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  static tiny_hsm_result_t state_poll_erds_from_list(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

 private:
  // Helper functions
  bool valid_polling_list_loaded();
  void save_polling_list_to_nv_store();
  void clear_nv_storage();
  void publish_mqtt_info();
  void start_mqtt_info_timer();
  void stop_mqtt_info_timer();
  void arm_timer(tiny_timer_ticks_t ticks);
  void disarm_lost_appliance_timer();
  void reset_lost_appliance_timer();
  void disarm_retry_timer();
  bool send_next_read_request();
  void add_erd_to_polling_list(tiny_erd_t erd);
  void send_next_poll_read_request();

  // Member variables
  uint32_t uptime_;
  tiny_erd_t last_erd_polled_successfully_;
  tiny_erd_t erd_polling_list_[GEA2_POLLING_LIST_MAX_SIZE];
  uint16_t polling_list_count_;
  tiny_timer_group_t* timer_group_;
  i_tiny_gea2_erd_client_t* erd_client_;
  i_mqtt_client_t* mqtt_client_;
  tiny_timer_t timer_;
  tiny_timer_t appliance_lost_timer_;
  tiny_timer_t mqtt_information_timer_;
  tiny_event_subscription_t mqtt_write_request_subscription_;
  tiny_event_subscription_t mqtt_disconnect_subscription_;
  tiny_event_subscription_t erd_client_activity_subscription_;
  tiny_hsm_t hsm_;
  std::set<tiny_erd_t>* erd_set_;
  tiny_gea2_erd_client_request_id_t request_id_;
  uint8_t erd_host_address_;
  uint8_t appliance_type_;
  const tiny_erd_t* appliance_erd_list_;
  uint16_t appliance_erd_list_count_;
  uint16_t erd_index_;

  // NVS storage
  ESPPreferenceObject pref_;
  
  struct NVSData {
    uint16_t polling_list_count;
    uint8_t erd_host_address;
    tiny_erd_t erd_polling_list[GEA2_POLLING_LIST_MAX_SIZE];
  };
};

}  // namespace geappliances_bridge
}  // namespace esphome
