
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

/*!
 * Maximum write payload accepted from MQTT. Same limit as the ERD cache:
 * the GEA3 max payload (255 on-wire minus 7 bytes of overhead). Pinned to
 * `ERD_CACHE_MAX_DATA_SIZE` by a static_assert in
 * geappliances_bridge_bridge_init.cpp.
 */
#define ERD_WRITE_MAX_DATA_SIZE 248

enum {
  /*!
   * Home Assistant automations routinely publish two commands back to back
   * (set a mode, then a setpoint) while the first is still on the bus.
   * Requests that arrive during an in-flight write are copied into this ring
   * and drained one at a time, so the existing single-outstanding-request
   * design stays intact. Only when the ring is full is a request rejected,
   * and it is rejected with a write_result so the caller is never left
   * waiting.
   *
   * Each slot costs ERD_WRITE_MAX_DATA_SIZE + 4 bytes, so a capacity of 4
   * adds just over 1 KB to the bridge.
   */
  erd_write_bridge_queue_capacity = 4
};

typedef struct {
  tiny_erd_t erd;
  /* 0xFF (broadcast) = primary board sentinel; see
   * mqtt_client_on_write_request_args_t. */
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
  /* Accepted writes waiting for the single in-flight slot, in arrival order.
   * The payload is copied at accept time because the write-request event
   * carries a pointer into the publisher's stack buffer. */
  erd_write_request_t queue[erd_write_bridge_queue_capacity];
  uint8_t queue_head;
  uint8_t queue_count;
  /* The one write outstanding on the bus. Valid only while the HSM is in
   * state_writing. */
  tiny_gea3_erd_client_request_id_t pending_request_id;
  tiny_erd_t pending_erd;
  /* 0xFF (broadcast) = primary board; any other value = secondary board
   * address. Mirrors the board_address in the write request args so the
   * result topic matches the topic the write was issued on. */
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
