
#ifndef i_mqtt_client_h
#define i_mqtt_client_h

#include <stddef.h>

#include "i_tiny_event.h"
#include "i_tiny_gea3_erd_client.h"
#include "tiny_erd.h"

typedef struct {
  tiny_erd_t erd;
  /* 0xFF (broadcast) = primary board (the bridge resolves it to the
   * detected host address); any other value = the secondary board address
   * parsed from the 0x{addr}_0x{erd} topic form. 0xFF is used because it
   * can never be a physical board address, keeping a 0x00 secondary board
   * unambiguous. */
  uint8_t board_address;
  uint8_t size;
  const void* value;
  /* Field-level read-modify-write. When is_field_write is true, the four
   * geometry fields describe the sub-field the command targets (parsed from
   * the …/write/{off}/{size}/{bitoff}/{bitsize} topic suffix); the bridge
   * reads the RMW base, patches only that field, and writes the full payload.
   * Full writes leave is_field_write false and the geometry unused. */
  bool is_field_write;
  uint16_t byte_offset;
  uint8_t byte_size;
  uint8_t bit_offset;
  uint8_t bit_size;
} mqtt_client_on_write_request_args_t;

struct i_mqtt_client_api_t;

typedef struct {
  const struct i_mqtt_client_api_t* api;
} i_mqtt_client_t;

typedef struct i_mqtt_client_api_t {
  void (*update_erd_write_result)(i_mqtt_client_t* self, tiny_erd_t erd, uint8_t board_address, bool success, tiny_gea3_erd_client_write_failure_reason_t failure_reason);

  i_tiny_event_t* (*on_write_request)(i_mqtt_client_t* self);

  i_tiny_event_t* (*on_mqtt_disconnect)(i_mqtt_client_t* self);

  i_tiny_event_t* (*on_mqtt_connect)(i_mqtt_client_t* self);

  bool (*publish_raw)(i_mqtt_client_t* self, const char* topic, const char* payload, size_t payload_len, bool retain);

  void (*subscribe)(i_mqtt_client_t* self, const char* topic, void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg), void* arg);

  void (*unsubscribe)(i_mqtt_client_t* self, const char* topic);
} i_mqtt_client_api_t;

static inline void mqtt_client_update_erd_write_result(i_mqtt_client_t* self, tiny_erd_t erd, uint8_t board_address, bool success, tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  self->api->update_erd_write_result(self, erd, board_address, success, failure_reason);
}

static inline i_tiny_event_t* mqtt_client_on_write_request(i_mqtt_client_t* self)
{
  return self->api->on_write_request(self);
}

static inline i_tiny_event_t* mqtt_client_on_mqtt_disconnect(i_mqtt_client_t* self)
{
  return self->api->on_mqtt_disconnect(self);
}

static inline i_tiny_event_t* mqtt_client_on_mqtt_connect(i_mqtt_client_t* self)
{
  return self->api->on_mqtt_connect(self);
}

static inline bool mqtt_client_publish_raw(i_mqtt_client_t* self, const char* topic, const char* payload, size_t payload_len, bool retain)
{
  return self->api->publish_raw(self, topic, payload, payload_len, retain);
}

static inline void mqtt_client_subscribe(i_mqtt_client_t* self, const char* topic, void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg), void* arg)
{
  self->api->subscribe(self, topic, callback, arg);
}

static inline void mqtt_client_unsubscribe(i_mqtt_client_t* self, const char* topic)
{
  self->api->unsubscribe(self, topic);
}

#endif
