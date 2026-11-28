#ifndef mqtt_client_double_hpp
#define mqtt_client_double_hpp

extern "C" {
#include "i_mqtt_client.h"
#include "tiny_event.h"
}

typedef struct {
  i_mqtt_client_t interface;

  tiny_event_t on_write_request;
  tiny_event_t on_mqtt_disconnect;
  tiny_event_t on_mqtt_connect;

  bool connected;
} mqtt_client_double_t;

void mqtt_client_double_init(mqtt_client_double_t* self);

void mqtt_client_double_trigger_write_request(
  mqtt_client_double_t* self,
  tiny_erd_t erd,
  uint8_t board_address,
  uint8_t size,
  const void* value);

void mqtt_client_double_trigger_mqtt_disconnect(
  mqtt_client_double_t* self);

void mqtt_client_double_trigger_mqtt_connect(
  mqtt_client_double_t* self);

void mqtt_client_double_set_connected(
  mqtt_client_double_t* self,
  bool connected);

bool mqtt_client_double_publish_raw(
  i_mqtt_client_t* self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain);

#endif
