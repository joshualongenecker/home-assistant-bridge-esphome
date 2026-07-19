/*!
 * @file
 * @brief
 */

#include "CppUTestExt/MockSupport.h"
#include "double/mqtt_client_double.hpp"

static void update_erd_write_result(i_mqtt_client_t* self, tiny_erd_t erd, bool success, tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  mock()
    .actualCall("update_erd_write_result")
    .onObject(self)
    .withParameter("erd", erd)
    .withParameter("success", success)
    .withParameter("failure_reason", failure_reason);
}

static i_tiny_event_t* on_write_request(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<mqtt_client_double_t*>(_self);
  return &self->on_write_request.interface;
}

static i_tiny_event_t* on_mqtt_disconnect(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<mqtt_client_double_t*>(_self);
  return &self->on_mqtt_disconnect.interface;
}

static i_tiny_event_t* on_mqtt_connect(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<mqtt_client_double_t*>(_self);
  return &self->on_mqtt_connect.interface;
}

static void subscribe(i_mqtt_client_t* self, const char* topic,
  void (*callback)(const char*, const char*, size_t, void*), void* arg)
{
  (void)self; (void)callback; (void)arg;
  mock()
    .actualCall("subscribe")
    .onObject(self)
    .withParameter("topic", topic);
}

static void unsubscribe(i_mqtt_client_t* self, const char* topic)
{
  mock()
    .actualCall("unsubscribe")
    .onObject(self)
    .withParameter("topic", topic);
}

static const i_mqtt_client_api_t api = {
  update_erd_write_result,
  on_write_request,
  on_mqtt_disconnect,
  on_mqtt_connect,
  mqtt_client_double_publish_raw,
  subscribe,
  unsubscribe
};

void mqtt_client_double_init(mqtt_client_double_t* self)
{
  self->interface.api = &api;
  tiny_event_init(&self->on_write_request);
  tiny_event_init(&self->on_mqtt_disconnect);
  tiny_event_init(&self->on_mqtt_connect);
}

void mqtt_client_double_trigger_write_request(
  mqtt_client_double_t* self,
  tiny_erd_t erd,
  uint8_t size,
  const void* value)
{
  mqtt_client_on_write_request_args_t args = { erd, size, value };
  tiny_event_publish(&self->on_write_request, &args);
}

void mqtt_client_double_trigger_mqtt_disconnect(
  mqtt_client_double_t* self)
{
  tiny_event_publish(&self->on_mqtt_disconnect, nullptr);
}

void mqtt_client_double_trigger_mqtt_connect(
  mqtt_client_double_t* self)
{
  tiny_event_publish(&self->on_mqtt_connect, nullptr);
}

bool mqtt_client_double_publish_raw(
  i_mqtt_client_t* _self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain)
{
  (void)_self;
  mock()
    .actualCall("publish_raw")
    .withParameter("topic", topic)
    .withParameterOfType("const char*", "payload", payload)
    .withParameter("payload_len", payload_len)
    .withParameter("retain", retain);
  return true;
}
