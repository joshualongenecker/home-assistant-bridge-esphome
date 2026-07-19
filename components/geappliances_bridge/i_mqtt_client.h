/*!
 * @file
 * @brief MQTT client interface for abstracting MQTT operations
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Define the abstract interface through which erd_bridge_subscribe and
//       erd_bridge_poll report ERD values and receive write commands,
//       keeping the bridge implementations independent of ESPHome.
//
// Responsibilities:
//   - Declare the i_mqtt_client_t vtable interface
//   - Provide inline wrappers for each interface method
//
// NOT responsible for:
//   - Any implementation (see EsphomeMqttClientAdapter)
//   - MQTT connection management
//
// Dependencies:
//   - i_tiny_event.h, i_tiny_gea3_erd_client.h, tiny_erd.h
// =============================================================================

#ifndef i_mqtt_client_h
#define i_mqtt_client_h

#include <stddef.h>

#include "i_tiny_event.h"
#include "i_tiny_gea3_erd_client.h"
#include "tiny_erd.h"

typedef struct {
  tiny_erd_t erd;
  uint8_t size;
  const void* value;
} mqtt_client_on_write_request_args_t;

struct i_mqtt_client_api_t;

typedef struct {
  const struct i_mqtt_client_api_t* api;
} i_mqtt_client_t;

typedef struct i_mqtt_client_api_t {
  void (*update_erd_write_result)(i_mqtt_client_t* self, tiny_erd_t erd, bool success, tiny_gea3_erd_client_write_failure_reason_t failure_reason);

  i_tiny_event_t* (*on_write_request)(i_mqtt_client_t* self);

  i_tiny_event_t* (*on_mqtt_disconnect)(i_mqtt_client_t* self);

  i_tiny_event_t* (*on_mqtt_connect)(i_mqtt_client_t* self);

  bool (*publish_raw)(i_mqtt_client_t* self, const char* topic, const char* payload, size_t payload_len, bool retain);

  void (*subscribe)(i_mqtt_client_t* self, const char* topic, void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg), void* arg);

  void (*unsubscribe)(i_mqtt_client_t* self, const char* topic);
} i_mqtt_client_api_t;



/*!
 * Provide the result for the most recently completed write request to an ERD.
 */
static inline void mqtt_client_update_erd_write_result(i_mqtt_client_t* self, tiny_erd_t erd, bool success, tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  self->api->update_erd_write_result(self, erd, success, failure_reason);
}

/*!
 * Event raised when a write request is received from the MQTT broker.
 */
static inline i_tiny_event_t* mqtt_client_on_write_request(i_mqtt_client_t* self)
{
  return self->api->on_write_request(self);
}

/*!
 * Event raised when the client disconnects from the MQTT broker.
 */
static inline i_tiny_event_t* mqtt_client_on_mqtt_disconnect(i_mqtt_client_t* self)
{
  return self->api->on_mqtt_disconnect(self);
}

/*!
 * Event raised when the client connects to the MQTT broker.
 */
static inline i_tiny_event_t* mqtt_client_on_mqtt_connect(i_mqtt_client_t* self)
{
  return self->api->on_mqtt_connect(self);
}

/*!
 * Publish a raw MQTT message (C-string topic and payload).
 * Returns true if the message was sent or queued, false if it was dropped.
 */
static inline bool mqtt_client_publish_raw(i_mqtt_client_t* self, const char* topic, const char* payload, size_t payload_len, bool retain)
{
  return self->api->publish_raw(self, topic, payload, payload_len, retain);
}

/*!
 * Subscribe to a topic with a raw C callback.
 */
static inline void mqtt_client_subscribe(i_mqtt_client_t* self, const char* topic, void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg), void* arg)
{
  self->api->subscribe(self, topic, callback, arg);
}

/*!
 * Unsubscribe from a topic.
 */
static inline void mqtt_client_unsubscribe(i_mqtt_client_t* self, const char* topic)
{
  self->api->unsubscribe(self, topic);
}

#endif
