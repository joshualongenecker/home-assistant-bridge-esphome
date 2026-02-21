/*!
 * @file
 * @brief Simplified GEA interface that only supports sending and receiving packets.
 *
 * @note that this interface does not support queueing so if a new message is sent before
 * the last send has been completed then the last send will be interrupted.
 */

#ifndef i_tiny_gea_interface_h
#define i_tiny_gea_interface_h

#include <stdbool.h>
#include "i_tiny_event.h"
#include "tiny_gea_packet.h"

typedef struct {
  const tiny_gea_packet_t* packet;
} tiny_gea_interface_on_receive_args_t;

typedef void (*tiny_gea_interface_send_callback_t)(void* context, tiny_gea_packet_t* packet);

struct i_tiny_gea_interface_api_t;

typedef struct {
  const struct i_tiny_gea_interface_api_t* api;
} i_tiny_gea_interface_t;

typedef struct i_tiny_gea_interface_api_t {
  bool (*send)(
    i_tiny_gea_interface_t* self,
    uint8_t destination,
    uint8_t payload_length,
    void* context,
    tiny_gea_interface_send_callback_t callback);

  bool (*forward)(
    i_tiny_gea_interface_t* self,
    uint8_t destination,
    uint8_t payload_length,
    void* context,
    tiny_gea_interface_send_callback_t callback);

  i_tiny_event_t* (*on_receive)(i_tiny_gea_interface_t* self);
} i_tiny_gea_interface_api_t;

/*!
 * Send a packet by getting direct access to the internal send buffer (given to
 * the client via the provided callback). Sets the source and destination addresses
 * of the packet automatically. If the requested payload size is too large then the
 * callback will not be invoked. Returns false if packet is dropped due to size
 * or activity.
 */
static inline bool tiny_gea_interface_send(
  i_tiny_gea_interface_t* self,
  uint8_t destination,
  uint8_t payload_length,
  void* context,
  tiny_gea_interface_send_callback_t callback)
{
  return self->api->send(self, destination, payload_length, context, callback);
}

/*!
 * Send a packet without setting source address.
 */
static inline bool tiny_gea_interface_forward(
  i_tiny_gea_interface_t* self,
  uint8_t destination,
  uint8_t payload_length,
  void* context,
  tiny_gea_interface_send_callback_t callback)
{
  return self->api->forward(self, destination, payload_length, context, callback);
}

/*!
 * Event raised when a packet is received.
 */
static inline i_tiny_event_t* tiny_gea_interface_on_receive(i_tiny_gea_interface_t* self)
{
  return self->api->on_receive(self);
}

#endif
