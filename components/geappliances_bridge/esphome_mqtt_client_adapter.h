// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Implement the i_mqtt_client_t interface for the bridge, publishing
//       ERD value updates to MQTT topics via ESPHome's global MQTT client.
//
// Responsibilities:
//   - Implement i_mqtt_client_t for the bridge and polling bridge
//   - Publish ERD updates to geappliances/{device_id}/erd/0x{ERD}/value topics
//   - Provide MQTT connect/disconnect events for publisher coordination
//
// NOT responsible for:
//   - Deciding which ERDs to publish (filtering is applied via ErdRegistry)
//   - Managing bridge lifecycle or startup phases
//
// Dependencies:
//   - i_mqtt_client.h (interface implemented here)
//   - ErdRegistry for valid-ERD filtering
//   - ESPHome MQTT client (esphome::mqtt::global_mqtt_client)
// =============================================================================

#pragma once
#include "esphome/components/mqtt/mqtt_client.h"


#include "erd_registry.h"

extern "C" {
#include "i_mqtt_client.h"
#include "tiny_event.h"
}


typedef struct {
  i_mqtt_client_t interface;
  const char* device_id;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;
  tiny_event_t on_mqtt_connect_event;
  // Optional ERD registry: when non-null, provides valid-ERD filtering,
  // string-ERD type detection, and registered-ERD tracking in one place.
  // Set via esphome_mqtt_client_adapter_set_erd_registry().
  esphome::geappliances_bridge::ErdRegistry* erd_registry;
  // Tracked write topic for unsubscribe on destroy.
  char write_topic_[128];
} esphome_mqtt_client_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id);

void esphome_mqtt_client_adapter_set_erd_registry(
  esphome_mqtt_client_adapter_t* self,
  esphome::geappliances_bridge::ErdRegistry* erd_registry);

void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self);
void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self);


void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self);
/*!
 * Publish raw MQTT message (C-string topic and payload).
 * Implements the i_mqtt_client_t publish_raw vtable slot.
 * Returns true if the message was sent or queued, false if it was dropped.
 */
bool esphome_mqtt_client_adapter_publish_raw(
  i_mqtt_client_t* self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain);

/*!
 * Subscribe to a topic with a raw C callback.
 * Implements the i_mqtt_client_t subscribe vtable slot.
 */
void esphome_mqtt_client_adapter_subscribe(
  i_mqtt_client_t* self,
  const char* topic,
  void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg),
  void* arg);

/*!
 * Unsubscribe from a topic.
 * Implements the i_mqtt_client_t unsubscribe vtable slot.
 */
void esphome_mqtt_client_adapter_unsubscribe(
  i_mqtt_client_t* self,
  const char* topic);

#ifdef __cplusplus
}
#endif