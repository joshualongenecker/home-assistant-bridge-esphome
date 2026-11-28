#ifndef erd_topic_h
#define erd_topic_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tiny_gea_constants.h" 

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ERD_TOPIC_OP_VALUE,
  ERD_TOPIC_OP_WRITE,
  ERD_TOPIC_OP_WRITE_RESULT
} erd_topic_op_t;

#define ERD_TOPIC_PRIMARY tiny_gea_broadcast_address

enum { ERD_TOPIC_BUF_SIZE = 128 };

int erd_topic_format(char* buf, size_t buf_size, const char* device_id,
                     uint8_t board_address, uint16_t erd, erd_topic_op_t op);

int erd_topic_format_write_filter(char* buf, size_t buf_size, const char* device_id);

bool erd_topic_parse(const char* topic, const char* device_id,
                     uint8_t* board_address, uint16_t* erd, erd_topic_op_t* op);

#ifdef __cplusplus
}
#endif

#endif
