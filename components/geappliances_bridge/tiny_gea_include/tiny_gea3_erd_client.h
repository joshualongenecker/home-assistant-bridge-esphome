/*!
 * @file
 * @brief
 */

#ifndef tiny_gea3_erd_client_h
#define tiny_gea3_erd_client_h

#include "i_tiny_gea3_erd_client.h"
#include "i_tiny_gea_interface.h"
#include "tiny_event.h"
#include "tiny_queue.h"
#include "tiny_ring_buffer.h"
#include "tiny_timer.h"

typedef struct {
  tiny_timer_ticks_t request_timeout;
  uint8_t request_retries;
} tiny_gea3_erd_client_configuration_t;

typedef struct {
  i_tiny_gea3_erd_client_t interface;

  tiny_event_subscription_t packet_received;
  tiny_queue_t request_queue;
  i_tiny_gea_interface_t* gea3_interface;
  tiny_timer_group_t* timer_group;
  tiny_timer_t request_retry_timer;
  tiny_event_t on_activity;
  const tiny_gea3_erd_client_configuration_t* configuration;
  uint8_t remaining_retries;
  uint8_t request_id;
  bool busy;
} tiny_gea3_erd_client_t;

/*!
 * Initialize an ERD client with a buffer for queueing requests. More memory
 * allocated means that more requests will be able to be queued.
 */
void tiny_gea3_erd_client_init(
  tiny_gea3_erd_client_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea_interface_t* gea3_interface,
  uint8_t* queue_buffer,
  size_t queue_buffer_size,
  const tiny_gea3_erd_client_configuration_t* configuration);

#endif
