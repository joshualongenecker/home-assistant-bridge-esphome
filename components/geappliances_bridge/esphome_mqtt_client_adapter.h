#pragma once

#include <string>

extern "C" {
#include "i_mqtt_client.h"
}

typedef struct {
  i_mqtt_client_t interface;
  std::string* device_id;
  i_tiny_event_t on_write_request_event;
  i_tiny_event_t on_mqtt_disconnect_event;
} esphome_mqtt_client_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id);

#ifdef __cplusplus
}
#endif
