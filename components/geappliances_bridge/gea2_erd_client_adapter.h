/*!
 * @file
 * @brief Adapter that wraps a GEA2 ERD client (i_tiny_gea2_erd_client_t)
 *        and presents it as a GEA3 ERD client (i_tiny_gea3_erd_client_t).
 *
 * This allows the existing mqtt_bridge_polling (which requires i_tiny_gea3_erd_client_t)
 * to operate over a GEA2 bus without modification.
 *
 * The GEA2 and GEA3 on_activity args are layout-compatible for read/write event
 * types (types 0-3), so GEA2 activity args can be safely re-published through
 * the adapter's GEA3-typed event without any data transformation.
 *
 * subscribe() and retain_subscription() are no-ops because GEA2 does not
 * support subscriptions; callers must use polling mode only.
 */

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

#ifdef __cplusplus
}
#endif
