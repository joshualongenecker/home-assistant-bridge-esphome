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
  tiny_hsm_state_t next_discovery_state;
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
  // Set to true once the HSM transitions into state_polling (all ERD
  // discovery phases have completed). Reset to false on appliance loss/
  // reconnect. Used externally to gate HA discovery until polling is steady.
  bool polling_list_complete;
  // Updated at each state entry with a human-readable name of the current HSM
  // state. Initialized to nullptr; callers may watch this for changes to emit
  // debug log messages without coupling mqtt_bridge.cpp to ESP logging headers.
  const char* current_state_name;
  // Optional pre-populated polling list from appliance API parsing.
  // When non-NULL, discovery states are skipped and this list is polled directly.
  const tiny_erd_t* api_parsed_list;
  uint16_t api_parsed_list_count;
  // Optional list of user-configured custom ERDs to poll in addition to the
  // standard list. Set after mqtt_bridge_polling_init(). Works with both
  // discovery mode and api_parsed_list mode.
  const tiny_erd_t* custom_erd_list;
  uint16_t custom_erd_list_count;
  // Optional pre-configured host address. When non-zero, state_identify_appliance
  // skips the broadcast read to 0xFF and uses this address directly, allowing the
  // bridge to start polling immediately with a known appliance address.
  // Must be set before calling mqtt_bridge_polling_init() (passed as a parameter).
  uint8_t host_address_override;
} mqtt_bridge_polling_t;

/*!
 * Initialize the MQTT polling bridge.
 * @param host_address_override When non-zero, state_identify_appliance skips the
 *   broadcast read and uses this address directly. Pass 0 for normal discovery.
 */
void mqtt_bridge_polling_init(
  mqtt_bridge_polling_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint32_t polling_interval_ms,
  bool only_publish_on_change,
  uint8_t host_address_override);

/*!
 * Destroy the MQTT polling bridge.
 */
void mqtt_bridge_polling_destroy(
  mqtt_bridge_polling_t* self);

#endif
