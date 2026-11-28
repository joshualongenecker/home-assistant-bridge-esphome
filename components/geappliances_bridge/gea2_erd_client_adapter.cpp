#include "gea2_erd_client_adapter.h"

static void on_gea2_activity(void* context, const void* args)
{
  auto self = static_cast<gea2_erd_client_adapter_t*>(context);
  tiny_event_publish(&self->on_activity, args);
}

static bool adapter_read(
  i_tiny_gea3_erd_client_t* _self,
  tiny_gea3_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd)
{
  auto self = reinterpret_cast<gea2_erd_client_adapter_t*>(_self);
  tiny_gea2_erd_client_request_id_t gea2_id;
  bool result = tiny_gea2_erd_client_read(self->gea2_client, &gea2_id, address, erd);
  if (request_id) *request_id = gea2_id;
  return result;
}

static bool adapter_write(
  i_tiny_gea3_erd_client_t* _self,
  tiny_gea3_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd,
  const void* data,
  uint8_t data_size)
{
  auto self = reinterpret_cast<gea2_erd_client_adapter_t*>(_self);
  tiny_gea2_erd_client_request_id_t gea2_id;
  bool result = tiny_gea2_erd_client_write(self->gea2_client, &gea2_id, address, erd, data, data_size);
  if (request_id) *request_id = gea2_id;
  return result;
}

static bool adapter_subscribe(i_tiny_gea3_erd_client_t* , uint8_t )
{
  return false;
}

static bool adapter_retain_subscription(i_tiny_gea3_erd_client_t* , uint8_t )
{
  return false;
}

static i_tiny_event_t* adapter_on_activity(i_tiny_gea3_erd_client_t* _self)
{
  auto self = reinterpret_cast<gea2_erd_client_adapter_t*>(_self);
  return &self->on_activity.interface;
}

static const i_tiny_gea3_erd_client_api_t adapter_api = {
  adapter_read,
  adapter_write,
  adapter_subscribe,
  adapter_retain_subscription,
  adapter_on_activity
};

extern "C" void gea2_erd_client_adapter_init(
  gea2_erd_client_adapter_t* self,
  i_tiny_gea2_erd_client_t* gea2_client)
{
  self->interface.api = &adapter_api;
  self->gea2_client = gea2_client;

  tiny_event_init(&self->on_activity);

  tiny_event_subscription_init(&self->gea2_sub, self, on_gea2_activity);
  tiny_event_subscribe(tiny_gea2_erd_client_on_activity(gea2_client), &self->gea2_sub);
}

extern "C" void gea2_erd_client_adapter_destroy(
  gea2_erd_client_adapter_t* self,
  i_tiny_gea2_erd_client_t* gea2_client)
{
  if (!self || !self->gea2_client) return;
  if (self->gea2_client != gea2_client) return;
  tiny_event_unsubscribe(
    tiny_gea2_erd_client_on_activity(gea2_client),
    &self->gea2_sub);
  self->gea2_client = nullptr;
}
