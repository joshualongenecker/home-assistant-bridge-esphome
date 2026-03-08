/*!
 * @file
 * @brief Adapter that wraps a GEA2 ERD client as a GEA3 ERD client.
 */

#include "gea2_erd_client_adapter.h"

// GEA2 and GEA3 activity args are layout-compatible for read/write event types
// (type 0=read_completed, 1=read_failed, 2=write_completed, 3=write_failed).
// Re-publish GEA2 activity args as GEA3 args via the adapter's event.
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

// GEA2 does not support subscriptions; return false so callers fall back to polling.
static bool adapter_subscribe(i_tiny_gea3_erd_client_t* /*self*/, uint8_t /*address*/)
{
  return false;
}

static bool adapter_retain_subscription(i_tiny_gea3_erd_client_t* /*self*/, uint8_t /*address*/)
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
