#ifndef tiny_gea2_erd_client_double_hpp
#define tiny_gea2_erd_client_double_hpp

extern "C" {
#include "i_tiny_gea2_erd_client.h"
#include "tiny_event.h"
}

typedef struct {
  i_tiny_gea2_erd_client_t interface;
  tiny_event_t on_activity;
} tiny_gea2_erd_client_double_t;

void tiny_gea2_erd_client_double_init(tiny_gea2_erd_client_double_t* self);

void tiny_gea2_erd_client_double_trigger_activity_event(
  tiny_gea2_erd_client_double_t* self,
  const tiny_gea2_erd_client_on_activity_args_t* args);

#endif
