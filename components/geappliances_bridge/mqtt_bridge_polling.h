/*!
 * @file
 * @brief Polls ERDs and publishes to MQTT server (polling mode)
 */

#ifndef mqtt_bridge_polling_h
#define mqtt_bridge_polling_h

#include "i_mqtt_client.h"
#include "i_tiny_gea3_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"
#include "erd_lists.h"

typedef struct {
  tiny_erd_t erd_polling_list[POLLING_LIST_MAX_SIZE];
  uint16_t polling_list_count;
  uint32_t polling_interval_ms;
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  i_mqtt_client_t* mqtt_client;
  tiny_timer_t timer;
  tiny_timer_t appliance_lost_timer;
  tiny_timer_t polling_timer;
  tiny_event_subscription_t mqtt_write_request_subscription;
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t erd_client_activity_subscription;
  tiny_hsm_t hsm;
  void* erd_set;
  void* erd_cache;
  tiny_gea3_erd_client_request_id_t request_id;
  uint8_t erd_host_address;
  uint8_t appliance_type;
  const tiny_erd_t* appliance_erd_list;
  uint16_t appliance_erd_list_count;
  uint16_t erd_index;
  uint16_t polling_retries;
  bool only_publish_on_change;
} mqtt_bridge_polling_t;

/*!
 * Initialize the MQTT polling bridge.
 */
void mqtt_bridge_polling_init(
  mqtt_bridge_polling_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint32_t polling_interval_ms,
  bool only_publish_on_change);

/*!
 * Destroy the MQTT polling bridge.
 */
void mqtt_bridge_polling_destroy(
  mqtt_bridge_polling_t* self);

#endif
