#ifndef erd_write_bridge_h
#define erd_write_bridge_h

#include "i_mqtt_client.h"
#include "i_tiny_gea3_erd_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "tiny_event.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"

#define ERD_WRITE_MAX_DATA_SIZE 248

enum {
  erd_write_bridge_queue_capacity = 4
};

typedef struct {
  tiny_erd_t erd;
  uint8_t board_address;
  uint8_t size;
  uint8_t value[ERD_WRITE_MAX_DATA_SIZE];
} erd_write_request_t;

typedef struct {
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  i_mqtt_client_t* mqtt_client;
  uint8_t erd_host_address;
  tiny_hsm_t hsm;
  tiny_event_subscription_t mqtt_write_request_subscription;
  tiny_event_subscription_t erd_client_activity_subscription;
  erd_write_request_t queue[erd_write_bridge_queue_capacity];
  uint8_t queue_head;
  uint8_t queue_count;
  tiny_gea3_erd_client_request_id_t pending_request_id;
  tiny_erd_t pending_erd;
  uint8_t pending_board_address;
} erd_write_bridge_t;

void erd_write_bridge_init(
  erd_write_bridge_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint8_t host_address);

void erd_write_bridge_destroy(
  erd_write_bridge_t* self);

#ifdef __cplusplus
}
#endif

#endif
