#include "erd_topic.h"

#include <cstdio>
#include <cstring>

static const char* erd_topic_op_to_string(erd_topic_op_t op)
{
  switch (op) {
    case ERD_TOPIC_OP_VALUE: return "value";
    case ERD_TOPIC_OP_WRITE: return "write";
    case ERD_TOPIC_OP_WRITE_RESULT: return "write_result";
  }
  return NULL;
}

int erd_topic_format(char* buf, size_t buf_size, const char* device_id,
                     uint8_t board_address, uint16_t erd, erd_topic_op_t op)
{
  if (buf == NULL || device_id == NULL) return -1;
  const char* op_str = erd_topic_op_to_string(op);
  if (op_str == NULL) return -1;

  int len;
  if (board_address == ERD_TOPIC_PRIMARY) {
    len = snprintf(buf, buf_size, "geappliances/%s/erd/0x%04x/%s",
                   device_id, erd, op_str);
  } else {
    len = snprintf(buf, buf_size, "geappliances/%s/erd/0x%02x_0x%04x/%s",
                   device_id, board_address, erd, op_str);
  }
  if (len < 0 || (size_t)len >= buf_size) return -1;
  return len;
}

int erd_topic_format_write_filter(char* buf, size_t buf_size, const char* device_id)
{
  if (buf == NULL || device_id == NULL) return -1;
  int len = snprintf(buf, buf_size, "geappliances/%s/erd/+/write", device_id);
  if (len < 0 || (size_t)len >= buf_size) return -1;
  return len;
}

static int erd_topic_hex_digit(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool erd_topic_parse_hex_fixed(const char* s, int width, uint32_t* out)
{
  uint32_t value = 0;
  for (int i = 0; i < width; i++) {
    int digit = erd_topic_hex_digit(s[i]);
    if (digit < 0) return false;
    value = (value << 4) | (uint32_t)digit;
  }
  *out = value;
  return true;
}

bool erd_topic_parse(const char* topic, const char* device_id,
                     uint8_t* board_address, uint16_t* erd, erd_topic_op_t* op)
{
  if (topic == NULL || device_id == NULL || board_address == NULL ||
      erd == NULL || op == NULL) {
    return false;
  }

  static const char prefix[] = "geappliances/";
  static const char middle[] = "/erd/0x";
  const size_t prefix_len = sizeof(prefix) - 1;
  const size_t middle_len = sizeof(middle) - 1;
  const size_t device_id_len = strlen(device_id);

  if (strncmp(topic, prefix, prefix_len) != 0) return false;
  const char* p = topic + prefix_len;
  if (strncmp(p, device_id, device_id_len) != 0) return false;
  p += device_id_len;
  if (strncmp(p, middle, middle_len) != 0) return false;
  p += middle_len;

  uint32_t addr = (uint32_t)ERD_TOPIC_PRIMARY;
  uint32_t erd_value = 0;
  uint32_t addr_value = 0;

  /* Secondary-board form: 0x{ADDR}_0x{ERD}/ — the "_" separator after two
   * hex digits is what distinguishes it from the legacy primary form. */
  if (erd_topic_parse_hex_fixed(p, 2, &addr_value) && p[2] == '_' &&
      p[3] == '0' && p[4] == 'x' &&
      erd_topic_parse_hex_fixed(p + 5, 4, &erd_value) && p[9] == '/') {
    /* 0xFF is the broadcast/primary sentinel and can never be a physical
     * board address (0x00-0xFE); reject it rather than silently aliasing
     * it onto the primary-board form. */
    if (addr_value == (uint32_t)ERD_TOPIC_PRIMARY) return false;
    addr = addr_value;
    p += 10;
  } else if (erd_topic_parse_hex_fixed(p, 4, &erd_value) && p[4] == '/') {
    p += 5;
  } else {
    return false;
  }

  for (int i = ERD_TOPIC_OP_VALUE; i <= ERD_TOPIC_OP_WRITE_RESULT; i++) {
    const char* op_str = erd_topic_op_to_string((erd_topic_op_t)i);
    if (strcmp(p, op_str) == 0) {
      *board_address = (uint8_t)addr;
      *erd = (uint16_t)erd_value;
      *op = (erd_topic_op_t)i;
      return true;
    }
  }
  return false;
}
