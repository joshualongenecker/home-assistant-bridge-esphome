/*!
 * @file
 * @brief Types for working with the GEA3 ERD API.
 */

#ifndef tiny_gea3_erd_api_h
#define tiny_gea3_erd_api_h

#include <stdint.h>

typedef uint8_t tiny_gea3_erd_api_request_id_t;

enum {
  tiny_gea3_erd_api_command_read_request = 0xA0,
  tiny_gea3_erd_api_command_read_response = 0xA1,
  tiny_gea3_erd_api_command_write_request = 0xA2,
  tiny_gea3_erd_api_command_write_response = 0xA3,
  tiny_gea3_erd_api_command_subscribe_all_request = 0xA4,
  tiny_gea3_erd_api_command_subscribe_all_response = 0xA5,
  tiny_gea3_erd_api_command_publication = 0xA6,
  tiny_gea3_erd_api_command_publication_acknowledgment = 0xA7,
  tiny_gea3_erd_api_command_subscription_host_startup = 0xA8,
};
typedef uint8_t tiny_gea3_erd_api_command_t;

enum {
  tiny_gea3_erd_api_read_result_success = 0,
  tiny_gea3_erd_api_read_result_unsupported_erd = 1,
  tiny_gea3_erd_api_read_result_busy = 2
};
typedef uint8_t tiny_gea3_erd_api_read_result_t;

enum {
  tiny_gea3_erd_api_write_result_success = 0,
  tiny_gea3_erd_api_write_result_unsupported_erd = 1,
  tiny_gea3_erd_api_write_result_incorrect_size = 2,
  tiny_gea3_erd_api_write_result_busy = 3
};
typedef uint8_t tiny_gea3_erd_api_write_result_t;

enum {
  tiny_gea3_erd_api_subscribe_all_request_type_add_subscription = 0,
  tiny_gea3_erd_api_subscribe_all_request_type_retain_subscription = 1
};
typedef uint8_t tiny_gea3_erd_api_subscribe_all_request_type_t;

enum {
  tiny_gea3_erd_api_subscribe_all_result_success = 0,
  tiny_gea3_erd_api_subscribe_all_result_no_available_subscriptions = 1
};
typedef uint8_t tiny_gea3_erd_api_subscribe_all_result_t;

typedef struct {
  uint8_t command;
  tiny_gea3_erd_api_request_id_t request_id;
  uint8_t erd_msb;
  uint8_t erd_lsb;
} tiny_gea3_erd_api_read_request_payload_t;

typedef struct {
  uint8_t command;
  tiny_gea3_erd_api_request_id_t request_id;
  tiny_gea3_erd_api_read_result_t result;
  uint8_t erd_msb;
  uint8_t erd_lsb;
} tiny_gea3_erd_api_read_unsupported_response_payload_t;

typedef struct {
  uint8_t command;
  tiny_gea3_erd_api_request_id_t request_id;
  tiny_gea3_erd_api_read_result_t result;
  uint8_t erd_msb;
  uint8_t erd_lsb;
  uint8_t data_size;
} tiny_gea3_erd_api_read_response_payload_header_t;

typedef struct {
  tiny_gea3_erd_api_read_response_payload_header_t header;
  uint8_t data[1];
} tiny_gea3_erd_api_read_response_payload_t;

typedef struct {
  uint8_t command;
  tiny_gea3_erd_api_request_id_t request_id;
  uint8_t erd_msb;
  uint8_t erd_lsb;
  uint8_t data_size;
} tiny_gea3_erd_api_write_request_payload_header_t;

typedef struct {
  tiny_gea3_erd_api_write_request_payload_header_t header;
  uint8_t data[1];
} tiny_gea3_erd_api_write_request_payload_t;

typedef struct {
  uint8_t command;
  tiny_gea3_erd_api_request_id_t request_id;
  tiny_gea3_erd_api_write_result_t result;
  uint8_t erd_msb;
  uint8_t erd_lsb;
} tiny_gea3_erd_api_write_response_payload_t;

typedef struct {
  uint8_t command;
  uint8_t context;
  tiny_gea3_erd_api_request_id_t request_id;
  uint8_t erd_count;
} tiny_gea3_erd_api_publication_header_t;

typedef struct {
  tiny_gea3_erd_api_publication_header_t header;
  uint8_t data[1];
} tiny_gea3_erd_api_publication_payload_t;

typedef struct {
  uint8_t command;
  tiny_gea3_erd_api_request_id_t request_id;
  tiny_gea3_erd_api_subscribe_all_request_type_t type;
} tiny_gea3_erd_api_subscribe_all_request_payload_t;

typedef struct {
  uint8_t command;
  tiny_gea3_erd_api_request_id_t request_id;
  tiny_gea3_erd_api_subscribe_all_result_t result;
} tiny_gea3_erd_api_subscribe_all_response_payload_t;

typedef struct {
  uint8_t command;
  uint8_t context;
  tiny_gea3_erd_api_request_id_t request_id;
  uint8_t erd_count;
  uint8_t erd_msb;
  uint8_t erd_lsb;
  uint8_t data_size;
} tiny_gea3_erd_api_single_erd_publication_header_t;

typedef struct {
  tiny_gea3_erd_api_single_erd_publication_header_t header;
  uint8_t data[1];
} tiny_gea3_erd_api_single_erd_publication_payload_t;

typedef struct {
  uint8_t command;
  uint8_t context;
  tiny_gea3_erd_api_request_id_t request_id;
} tiny_gea3_erd_api_publication_acknowledgement_payload_t;

#endif
