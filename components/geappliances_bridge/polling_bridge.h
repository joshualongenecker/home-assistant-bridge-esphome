/*!
 * @file
 * @brief Polls ERDs and publishes them to an MQTT server.
 */

#ifndef polling_bridge_h
#define polling_bridge_h

#include "i_mqtt_client.h"
#include "i_tiny_gea3_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"

typedef struct {
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  i_mqtt_client_t* mqtt_client;
  tiny_timer_t timer;
  tiny_timer_t polling_timer;
  tiny_timer_t appliance_lost_timer;
  tiny_event_subscription_t mqtt_write_request_subscription;
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t erd_client_activity_subscription;
  void* erd_set;
  tiny_hsm_t hsm;
  
  // Configuration
  uint32_t polling_interval_ms;
  
  // State
  uint8_t appliance_type;
  uint8_t erd_host_address;
  tiny_gea3_erd_client_request_id_t request_id;
  
  // ERD discovery
  const tiny_erd_t* current_erd_list;
  size_t current_erd_list_count;
  size_t erd_index;
  
  // Polling list
  tiny_erd_t erd_polling_list[512];
  size_t polling_list_count;
  size_t polling_retries;
  tiny_erd_t last_erd_polled_successfully;
} polling_bridge_t;

/*!
 * Initialize the polling bridge.
 */
void polling_bridge_init(
  polling_bridge_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint32_t polling_interval_ms);

/*!
 * Destroy the polling bridge.
 */
void polling_bridge_destroy(
  polling_bridge_t* self);

#endif
