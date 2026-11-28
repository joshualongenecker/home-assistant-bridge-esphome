#pragma once

extern "C" {
#include "i_tiny_gea2_erd_client.h"
#include "i_tiny_gea3_erd_client.h"
#include "tiny_event.h"
}

typedef struct {
  i_tiny_gea3_erd_client_t interface;

  i_tiny_gea2_erd_client_t* gea2_client;

  tiny_event_t on_activity;
  tiny_event_subscription_t gea2_sub;
} gea2_erd_client_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void gea2_erd_client_adapter_init(
  gea2_erd_client_adapter_t* self,
  i_tiny_gea2_erd_client_t* gea2_client);

void gea2_erd_client_adapter_destroy(
  gea2_erd_client_adapter_t* self,
  i_tiny_gea2_erd_client_t* gea2_client);

#ifdef __cplusplus
}
#endif
