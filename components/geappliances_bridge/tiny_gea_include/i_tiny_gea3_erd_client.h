/*!
 * @file
 * @brief Interface for acting as an ERD client. Supports reads, writes, and subscriptions.
 */

#ifndef i_tiny_gea3_erd_client_h
#define i_tiny_gea3_erd_client_h

#include <stdint.h>
#include "i_tiny_event.h"
#include "tiny_erd.h"

enum {
  tiny_gea3_erd_client_activity_type_read_completed,
  tiny_gea3_erd_client_activity_type_read_failed,
  tiny_gea3_erd_client_activity_type_write_completed,
  tiny_gea3_erd_client_activity_type_write_failed,
  tiny_gea3_erd_client_activity_type_subscription_added_or_retained,
  tiny_gea3_erd_client_activity_type_subscribe_failed,
  tiny_gea3_erd_client_activity_type_subscription_publication_received,
  tiny_gea3_erd_client_activity_type_subscription_host_came_online
};
typedef uint8_t tiny_gea3_erd_client_activity_type_t;

enum {
  tiny_gea3_erd_client_read_failure_reason_retries_exhausted,
  tiny_gea3_erd_client_read_failure_reason_not_supported
};
typedef uint8_t tiny_gea3_erd_client_read_failure_reason_t;

enum {
  tiny_gea3_erd_client_write_failure_reason_retries_exhausted,
  tiny_gea3_erd_client_write_failure_reason_not_supported,
  tiny_gea3_erd_client_write_failure_reason_incorrect_size
};
typedef uint8_t tiny_gea3_erd_client_write_failure_reason_t;

typedef uint8_t tiny_gea3_erd_client_request_id_t;

typedef struct {
  tiny_gea3_erd_client_activity_type_t type;
  uint8_t address;

  union {
    /*!
     * @warning Data will be in big endian. Implementations will not have enough information to
     * swap on the client's behalf.
     */
    struct {
      tiny_gea3_erd_client_request_id_t request_id;
      tiny_erd_t erd;
      const void* data;
      uint8_t data_size;
    } read_completed;

    struct {
      tiny_gea3_erd_client_request_id_t request_id;
      tiny_erd_t erd;
      tiny_gea3_erd_client_read_failure_reason_t reason;
    } read_failed;

    struct {
      tiny_gea3_erd_client_request_id_t request_id;
      tiny_erd_t erd;
      const void* data;
      uint8_t data_size;
    } write_completed;

    struct {
      tiny_gea3_erd_client_request_id_t request_id;
      tiny_erd_t erd;
      const void* data;
      uint8_t data_size;
      tiny_gea3_erd_client_write_failure_reason_t reason;
    } write_failed;

    /*!
     * @warning Data will be in big endian. Implementations will not have enough information to
     * swap on the client's behalf.
     */
    struct {
      tiny_erd_t erd;
      const void* data;
      uint8_t data_size;
    } subscription_publication_received;
  };
} tiny_gea3_erd_client_on_activity_args_t;

struct i_tiny_gea3_erd_client_api_t;

typedef struct {
  const struct i_tiny_gea3_erd_client_api_t* api;
} i_tiny_gea3_erd_client_t;

typedef struct i_tiny_gea3_erd_client_api_t {
  bool (*read)(
    i_tiny_gea3_erd_client_t* self,
    tiny_gea3_erd_client_request_id_t* request_id,
    uint8_t address,
    tiny_erd_t erd);

  bool (*write)(
    i_tiny_gea3_erd_client_t* self,
    tiny_gea3_erd_client_request_id_t* request_id,
    uint8_t address,
    tiny_erd_t erd,
    const void* data,
    uint8_t data_size);

  bool (*subscribe)(i_tiny_gea3_erd_client_t* self, uint8_t address);

  bool (*retain_subscription)(i_tiny_gea3_erd_client_t* self, uint8_t address);

  i_tiny_event_t* (*on_activity)(i_tiny_gea3_erd_client_t* self);
} i_tiny_gea3_erd_client_api_t;

/*!
 * Send a read ERD request to ERD host. Returns true if the request could be queued, false otherwise.
 */
static inline bool tiny_gea3_erd_client_read(
  i_tiny_gea3_erd_client_t* self,
  tiny_gea3_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd)
{
  return self->api->read(self, request_id, address, erd);
}

/*!
 * Send a write ERD request to an ERD host. Returns true if the request could be queued, false otherwise.
 * @warning Data must already be in big endian. Implementers will not have enough information to
 *    swap on the client's behalf.
 */
static inline bool tiny_gea3_erd_client_write(
  i_tiny_gea3_erd_client_t* self,
  tiny_gea3_erd_client_request_id_t* request_id,
  uint8_t address,
  tiny_erd_t erd,
  const void* data,
  uint8_t data_size)
{
  return self->api->write(self, request_id, address, erd, data, data_size);
}

/*!
 * Send a subscribe request to an ERD host. Returns true if the request could be queued, false otherwise.
 */
static inline bool tiny_gea3_erd_client_subscribe(i_tiny_gea3_erd_client_t* self, uint8_t address)
{
  return self->api->subscribe(self, address);
}

/*!
 * Send a retain subscription (subscription keep-alive) request to an ERD host. Returns true if the request could be queued, false otherwise.
 */
static inline bool tiny_gea3_erd_client_retain_subscription(i_tiny_gea3_erd_client_t* self, uint8_t address)
{
  return self->api->retain_subscription(self, address);
}

/*!
 * Event that is raised when a read, write, subscribe request is received, when a
 * request fails, and when a subscription host comes online.
 */
static inline i_tiny_event_t* tiny_gea3_erd_client_on_activity(i_tiny_gea3_erd_client_t* self)
{
  return self->api->on_activity(self);
}

#endif
