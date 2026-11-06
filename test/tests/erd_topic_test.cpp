/*!
 * @file
 * @brief Unit tests for the shared ERD MQTT topic grammar (erd_topic.h).
 *
 * Pins the golden topic strings that the publisher, the MQTT adapter, and
 * HA discovery all emit through erd_topic_format(), and the accept/reject
 * behavior of erd_topic_parse() — including the 0xFF primary sentinel and
 * the secondary-board 0x00 case that must stay distinct from it.
 */

#include "CppUTest/TestHarness.h"

#include "erd_topic.h"

#include <cstring>

TEST_GROUP(ErdTopic) {
  char buf[ERD_TOPIC_BUF_SIZE];

  void setup() override {
    memset(buf, 0xAA, sizeof(buf));
  }

  void assert_format(int expected_len, const char* expected, int actual_len) {
    LONGS_EQUAL(expected_len, actual_len);
    STRCMP_EQUAL(expected, buf);
  }
};

/* ------------------------------------------------------------------ */
/* Format: golden strings per operation and board addressing            */
/* ------------------------------------------------------------------ */

TEST(ErdTopic, formats_value_topic_for_primary_board) {
  int len = erd_topic_format(buf, sizeof(buf), "dev", ERD_TOPIC_PRIMARY, 0x3015, ERD_TOPIC_OP_VALUE);
  assert_format((int)strlen("geappliances/dev/erd/0x3015/value"),
                "geappliances/dev/erd/0x3015/value", len);
}

TEST(ErdTopic, formats_value_topic_for_secondary_board) {
  int len = erd_topic_format(buf, sizeof(buf), "dev", 0x10, 0x3015, ERD_TOPIC_OP_VALUE);
  assert_format((int)strlen("geappliances/dev/erd/0x10_0x3015/value"),
                "geappliances/dev/erd/0x10_0x3015/value", len);
}

TEST(ErdTopic, secondary_board_at_zero_stays_distinct_from_primary_sentinel) {
  int len = erd_topic_format(buf, sizeof(buf), "dev", 0x00, 0x3015, ERD_TOPIC_OP_VALUE);
  assert_format((int)strlen("geappliances/dev/erd/0x00_0x3015/value"),
                "geappliances/dev/erd/0x00_0x3015/value", len);
}

TEST(ErdTopic, formats_write_topic) {
  int len = erd_topic_format(buf, sizeof(buf), "dev", ERD_TOPIC_PRIMARY, 0x7701, ERD_TOPIC_OP_WRITE);
  assert_format((int)strlen("geappliances/dev/erd/0x7701/write"),
                "geappliances/dev/erd/0x7701/write", len);
}

TEST(ErdTopic, formats_write_result_topic_for_secondary_board) {
  int len = erd_topic_format(buf, sizeof(buf), "dev", 0xa1, 0x7701, ERD_TOPIC_OP_WRITE_RESULT);
  assert_format((int)strlen("geappliances/dev/erd/0xa1_0x7701/write_result"),
                "geappliances/dev/erd/0xa1_0x7701/write_result", len);
}

TEST(ErdTopic, formats_write_result_topic_for_primary_board) {
  int len = erd_topic_format(buf, sizeof(buf), "dev", ERD_TOPIC_PRIMARY, 0x7701, ERD_TOPIC_OP_WRITE_RESULT);
  assert_format((int)strlen("geappliances/dev/erd/0x7701/write_result"),
                "geappliances/dev/erd/0x7701/write_result", len);
}

TEST(ErdTopic, formats_hex_fields_lowercase_and_zero_padded) {
  int len = erd_topic_format(buf, sizeof(buf), "dev", 0x0a, 0x0abc, ERD_TOPIC_OP_VALUE);
  assert_format((int)strlen("geappliances/dev/erd/0x0a_0x0abc/value"),
                "geappliances/dev/erd/0x0a_0x0abc/value", len);
}

TEST(ErdTopic, formats_write_filter) {
  int len = erd_topic_format_write_filter(buf, sizeof(buf), "dev");
  assert_format((int)strlen("geappliances/dev/erd/+/write"),
                "geappliances/dev/erd/+/write", len);
}

/* ------------------------------------------------------------------ */
/* Format: failure modes                                                */
/* ------------------------------------------------------------------ */

/* strlen(full) is the formatted length; the NUL terminator needs one more
 * byte, so a buffer of exactly strlen(full) must be rejected. */
TEST(ErdTopic, format_rejects_buffer_exactly_one_byte_short) {
  const char* full = "geappliances/dev/erd/0x3015/value";
  char small[ERD_TOPIC_BUF_SIZE];
  memset(small, 0xAA, sizeof(small));
  LONGS_EQUAL(-1, erd_topic_format(small, strlen(full), "dev", ERD_TOPIC_PRIMARY, 0x3015, ERD_TOPIC_OP_VALUE));
}

TEST(ErdTopic, format_accepts_buffer_exactly_fitting) {
  const char* full = "geappliances/dev/erd/0x3015/value";
  char exact[64];
  int len = erd_topic_format(exact, strlen(full) + 1, "dev", ERD_TOPIC_PRIMARY, 0x3015, ERD_TOPIC_OP_VALUE);
  LONGS_EQUAL((int)strlen(full), len);
  STRCMP_EQUAL(full, exact);
}

TEST(ErdTopic, format_rejects_invalid_op) {
  LONGS_EQUAL(-1, erd_topic_format(buf, sizeof(buf), "dev", ERD_TOPIC_PRIMARY, 0x3015, (erd_topic_op_t)99));
}

TEST(ErdTopic, format_rejects_null_arguments) {
  LONGS_EQUAL(-1, erd_topic_format(NULL, sizeof(buf), "dev", ERD_TOPIC_PRIMARY, 0x3015, ERD_TOPIC_OP_VALUE));
  LONGS_EQUAL(-1, erd_topic_format(buf, sizeof(buf), NULL, ERD_TOPIC_PRIMARY, 0x3015, ERD_TOPIC_OP_VALUE));
}

TEST(ErdTopic, parse_rejects_null_arguments) {
  uint8_t addr;
  uint16_t erd;
  erd_topic_op_t op;
  CHECK_FALSE(erd_topic_parse(NULL, "dev", &addr, &erd, &op));
  CHECK_FALSE(erd_topic_parse("geappliances/dev/erd/0x3015/value", NULL, &addr, &erd, &op));
  CHECK_FALSE(erd_topic_parse("geappliances/dev/erd/0x3015/value", "dev", NULL, &erd, &op));
  CHECK_FALSE(erd_topic_parse("geappliances/dev/erd/0x3015/value", "dev", &addr, NULL, &op));
  CHECK_FALSE(erd_topic_parse("geappliances/dev/erd/0x3015/value", "dev", &addr, &erd, NULL));
}

/* ------------------------------------------------------------------ */
/* Parse: accepted forms                                                */
/* ------------------------------------------------------------------ */

TEST(ErdTopic, parses_primary_value_topic_to_sentinel) {
  uint8_t addr = 0x55;
  uint16_t erd = 0;
  erd_topic_op_t op = ERD_TOPIC_OP_WRITE;
  CHECK(erd_topic_parse("geappliances/dev/erd/0x3015/value", "dev", &addr, &erd, &op));
  UNSIGNED_LONGS_EQUAL(ERD_TOPIC_PRIMARY, addr);
  UNSIGNED_LONGS_EQUAL(0x3015, erd);
  LONGS_EQUAL(ERD_TOPIC_OP_VALUE, op);
}

TEST(ErdTopic, parses_secondary_write_topic) {
  uint8_t addr = 0x55;
  uint16_t erd = 0;
  erd_topic_op_t op = ERD_TOPIC_OP_VALUE;
  CHECK(erd_topic_parse("geappliances/dev/erd/0x10_0x3015/write", "dev", &addr, &erd, &op));
  UNSIGNED_LONGS_EQUAL(0x10, addr);
  UNSIGNED_LONGS_EQUAL(0x3015, erd);
  LONGS_EQUAL(ERD_TOPIC_OP_WRITE, op);
}

TEST(ErdTopic, parses_secondary_write_result_topic_at_address_zero) {
  uint8_t addr = 0x55;
  uint16_t erd = 0;
  erd_topic_op_t op = ERD_TOPIC_OP_VALUE;
  CHECK(erd_topic_parse("geappliances/dev/erd/0x00_0x3015/write_result", "dev", &addr, &erd, &op));
  UNSIGNED_LONGS_EQUAL(0x00, addr);
  UNSIGNED_LONGS_EQUAL(0x3015, erd);
  LONGS_EQUAL(ERD_TOPIC_OP_WRITE_RESULT, op);
}

TEST(ErdTopic, parse_accepts_uppercase_hex_fields) {
  uint8_t addr = 0;
  uint16_t erd = 0;
  erd_topic_op_t op;
  CHECK(erd_topic_parse("geappliances/dev/erd/0xAB_0xCD12/value", "dev", &addr, &erd, &op));
  UNSIGNED_LONGS_EQUAL(0xab, addr);
  UNSIGNED_LONGS_EQUAL(0xcd12, erd);
  LONGS_EQUAL(ERD_TOPIC_OP_VALUE, op);
}

/* ------------------------------------------------------------------ */
/* Parse: rejected forms                                                */
/* ------------------------------------------------------------------ */

static bool parse_rejects(const char* topic) {
  uint8_t addr = 0x55;
  uint16_t erd = 0x5555;
  erd_topic_op_t op = ERD_TOPIC_OP_VALUE;
  bool ok = erd_topic_parse(topic, "dev", &addr, &erd, &op);
  /* Outputs must be untouched on rejection. */
  if (!ok) {
    UNSIGNED_LONGS_EQUAL(0x55, addr);
    UNSIGNED_LONGS_EQUAL(0x5555, erd);
    LONGS_EQUAL(ERD_TOPIC_OP_VALUE, op);
  }
  return !ok;
}

TEST(ErdTopic, parse_rejects_malformed_topics) {
  CHECK(parse_rejects(""));
  CHECK(parse_rejects("geappliances/dev/erd/0x3015"));
  CHECK(parse_rejects("geappliances/dev/erd/0x3015/"));
  CHECK(parse_rejects("otherprefix/dev/erd/0x3015/value"));
  CHECK(parse_rejects("geappliances/other/erd/0x3015/value"));
  /* device_id prefix collision: "devx" must not match device "dev" */
  CHECK(parse_rejects("geappliances/devx/erd/0x3015/value"));
  CHECK(parse_rejects("geappliances/dev/0x3015/value"));
  CHECK(parse_rejects("geappliances/dev/erd/x3015/value"));
  CHECK(parse_rejects("geappliances/dev/erd/0x301/value"));
  CHECK(parse_rejects("geappliances/dev/erd/0x30155/value"));
  CHECK(parse_rejects("geappliances/dev/erd/0x1_0x3015/value"));
  CHECK(parse_rejects("geappliances/dev/erd/0x10_3015/value"));
  CHECK(parse_rejects("geappliances/dev/erd/0x10_0x301/value"));
  CHECK(parse_rejects("geappliances/dev/erd/0x10__0x3015/value"));
  CHECK(parse_rejects("geappliances/dev/erd/0x3015/valuex"));
  CHECK(parse_rejects("geappliances/dev/erd/0x3015/foo"));
  CHECK(parse_rejects("geappliances/dev/erd/0x3015/write_resultx"));
  /* The literal "0x" prefixes must be lowercase; uppercase X is not the
   * grammar even though the hex digits themselves are case-insensitive. */
  CHECK(parse_rejects("geappliances/dev/erd/0X10_0x3015/value"));
  CHECK(parse_rejects("geappliances/dev/erd/0x10_0X3015/value"));
}

TEST(ErdTopic, parse_rejects_secondary_address_matching_primary_sentinel) {
  /* 0xFF can never be a physical board address; it must not silently alias
   * onto the primary-board form the way the pre-refactor sscanf parse did. */
  CHECK(parse_rejects("geappliances/dev/erd/0xff_0x3015/write"));
}

/* ------------------------------------------------------------------ */
/* Format -> parse round trip                                           */
/* ------------------------------------------------------------------ */

TEST(ErdTopic, format_parse_round_trip_matrix) {
  static const uint8_t addrs[] = { ERD_TOPIC_PRIMARY, 0x00, 0x01, 0x7f, 0xfe };
  static const uint16_t erds[] = { 0x0000, 0x3015, 0xffff };
  static const erd_topic_op_t ops[] = { ERD_TOPIC_OP_VALUE, ERD_TOPIC_OP_WRITE, ERD_TOPIC_OP_WRITE_RESULT };

  for (size_t a = 0; a < sizeof(addrs) / sizeof(addrs[0]); a++) {
    for (size_t e = 0; e < sizeof(erds) / sizeof(erds[0]); e++) {
      for (size_t o = 0; o < sizeof(ops) / sizeof(ops[0]); o++) {
        int len = erd_topic_format(buf, sizeof(buf), "dev", addrs[a], erds[e], ops[o]);
        CHECK_TRUE(len >= 0);

        uint8_t addr = 0;
        uint16_t erd = 0;
        erd_topic_op_t op;
        CHECK_TRUE(erd_topic_parse(buf, "dev", &addr, &erd, &op));
        UNSIGNED_LONGS_EQUAL(addrs[a], addr);
        UNSIGNED_LONGS_EQUAL(erds[e], erd);
        LONGS_EQUAL(ops[o], op);
      }
    }
  }
}

TEST(ErdTopic, long_device_id_fits_exactly_and_one_more_byte_rejects) {
  /* A device_id near ERD_TOPIC_BUF_SIZE is the only realistic way
   * erd_topic_format can fail in production. The secondary form fixes 35
   * chars + NUL, so 92 device_id bytes fit exactly and 93 overflow. */
  char device_id[93];
  memset(device_id, 'd', sizeof(device_id) - 1);
  device_id[sizeof(device_id) - 1] = '\0';

  int len = erd_topic_format(buf, sizeof(buf), device_id, 0x10, 0x3015, ERD_TOPIC_OP_VALUE);
  LONGS_EQUAL(ERD_TOPIC_BUF_SIZE - 1, len);

  uint8_t addr = 0;
  uint16_t erd = 0;
  erd_topic_op_t op;
  CHECK_TRUE(erd_topic_parse(buf, device_id, &addr, &erd, &op));
  UNSIGNED_LONGS_EQUAL(0x10, addr);
  UNSIGNED_LONGS_EQUAL(0x3015, erd);
  LONGS_EQUAL(ERD_TOPIC_OP_VALUE, op);

  char longer[94];
  memset(longer, 'd', sizeof(longer) - 1);
  longer[sizeof(longer) - 1] = '\0';
  LONGS_EQUAL(-1, erd_topic_format(buf, sizeof(buf), longer, 0x10, 0x3015, ERD_TOPIC_OP_VALUE));
}
