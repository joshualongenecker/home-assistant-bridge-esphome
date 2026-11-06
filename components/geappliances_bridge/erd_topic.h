/*
 * Single definition of the ERD MQTT topic grammar:
 *
 *   geappliances/{device_id}/erd/0x{ERD}/{op}           primary board (legacy)
 *   geappliances/{device_id}/erd/0x{ADDR}_0x{ERD}/{op}  secondary board
 *
 * {ADDR} is two hex digits, {ERD} is four, both lowercase on output.
 * A board_address of ERD_TOPIC_PRIMARY (the GEA broadcast address 0xFF, which
 * can never be a physical board address) selects the legacy no-prefix form.
 * Parsing accepts upper- and lowercase hex fields; the literal "0x" prefixes
 * themselves must be lowercase (the writers in this component always emit
 * lowercase). Parsing rejects a secondary board address of 0xFF; a
 * primary-board topic parses to ERD_TOPIC_PRIMARY.
 */

#ifndef erd_topic_h
#define erd_topic_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tiny_gea_constants.h" /* ERD_TOPIC_PRIMARY sentinel */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ERD_TOPIC_OP_VALUE,
  ERD_TOPIC_OP_WRITE,
  ERD_TOPIC_OP_WRITE_RESULT
} erd_topic_op_t;

/* The GEA broadcast address doubles as the "primary board" sentinel in the
 * topic grammar; it can never be a physical board address (0x00-0xFE). */
#define ERD_TOPIC_PRIMARY tiny_gea_broadcast_address

/* Sizing for topic buffers; matches the buffers this grammar was migrated
 * from (128 bytes covers the longest device_id in use with margin). */
enum { ERD_TOPIC_BUF_SIZE = 128 };

/*
 * Formats an ERD topic into buf. Returns the number of characters written
 * (excluding the NUL) on success, or -1 if the arguments are invalid or the
 * result would not fit in buf_size. On -1 the caller must treat buf as
 * discarded (a partial string may have been written).
 */
int erd_topic_format(char* buf, size_t buf_size, const char* device_id,
                     uint8_t board_address, uint16_t erd, erd_topic_op_t op);

/*
 * Formats the wildcard subscribe filter that matches every write topic for a
 * device: geappliances/{device_id}/erd/+/write
 * Return semantics match erd_topic_format().
 */
int erd_topic_format_write_filter(char* buf, size_t buf_size, const char* device_id);

/*
 * Parses an ERD topic. The topic must match the grammar exactly: the
 * geappliances/{device_id}/erd/ prefix is anchored, the hex fields must be
 * exactly 2 (address) and 4 (ERD) digits, and the operation suffix must be
 * one of value/write/write_result. On success writes the parsed values and
 * returns true; a primary-board topic yields *board_address ==
 * ERD_TOPIC_PRIMARY. Returns false on any mismatch (outputs untouched).
 */
bool erd_topic_parse(const char* topic, const char* device_id,
                     uint8_t* board_address, uint16_t* erd, erd_topic_op_t* op);

#ifdef __cplusplus
}
#endif

#endif
