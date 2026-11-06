
#pragma once
#include "esphome/components/mqtt/mqtt_client.h"

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
  // Tracked write topic for unsubscribe on destroy.
  char write_topic_[128];
} esphome_mqtt_client_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id);

void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self);
void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self);
bool esphome_mqtt_client_adapter_publish_raw(
  i_mqtt_client_t* self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain);

void esphome_mqtt_client_adapter_subscribe(
  i_mqtt_client_t* self,
  const char* topic,
  void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg),
  void* arg);

void esphome_mqtt_client_adapter_unsubscribe(
  i_mqtt_client_t* self,
  const char* topic);

#ifdef __cplusplus
}
#endif