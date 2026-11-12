/*!
 * @file
 * @brief Unit tests for the board-distribution tally (erd_bridge_common.h).
 *
 * The tally backs the one-line boot summaries that replaced the per-ERD dumps,
 * so what matters is the rendered string: the 0xFF primary sentinel must read
 * as "primary" (it is never a physical board address), boards must keep their
 * first-seen order, and a board past the tracked slots must collapse into a
 * count rather than grow the line.
 */

#include "CppUTest/TestHarness.h"

#include "erd_bridge_common.h"

#include <cstring>

TEST_GROUP(BoardTally) {
  board_tally_t tally;
  char buf[BOARD_TALLY_BUF_SIZE];

  void setup() override {
    board_tally_init(&tally);
    memset(buf, 0xAA, sizeof(buf));
  }

  const char* format() {
    board_tally_format(&tally, buf, sizeof(buf));
    return buf;
  }
};

TEST(BoardTally, empty_tally_formats_as_none) {
  STRCMP_EQUAL("none", format());
  LONGS_EQUAL(4, board_tally_format(&tally, buf, sizeof(buf)));
}

TEST(BoardTally, counts_every_entry_on_one_board) {
  board_tally_add(&tally, 0xC0);
  board_tally_add(&tally, 0xC0);
  board_tally_add(&tally, 0xC0);
  STRCMP_EQUAL("0xC0 3", format());
}

TEST(BoardTally, primary_sentinel_reads_as_primary_not_0xff) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  STRCMP_EQUAL("primary 1", format());
}

TEST(BoardTally, boards_appear_in_first_seen_order) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  board_tally_add(&tally, 0xC0);
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  board_tally_add(&tally, 0xC1);
  board_tally_add(&tally, 0xC0);
  STRCMP_EQUAL("primary 2, 0xC0 2, 0xC1 1", format());
}

TEST(BoardTally, a_physical_address_of_zero_is_not_the_primary_sentinel) {
  // 0x00 is a real board address; only 0xFF means "the primary board".
  board_tally_add(&tally, 0x00);
  STRCMP_EQUAL("0x00 1", format());
}

TEST(BoardTally, boards_past_the_tracked_slots_collapse_into_a_count) {
  board_tally_add(&tally, 0xC0);
  board_tally_add(&tally, 0xC1);
  board_tally_add(&tally, 0xC2);
  board_tally_add(&tally, 0xC3);
  board_tally_add(&tally, 0xC4);
  board_tally_add(&tally, 0xC4);
  board_tally_add(&tally, 0xC5);
  STRCMP_EQUAL("0xC0 1, 0xC1 1, 0xC2 1, 0xC3 1, +3 more", format());
}

TEST(BoardTally, untracked_entries_do_not_displace_tracked_boards) {
  // Once the slots are full, repeat entries of a tracked board still count.
  board_tally_add(&tally, 0xC0);
  board_tally_add(&tally, 0xC1);
  board_tally_add(&tally, 0xC2);
  board_tally_add(&tally, 0xC3);
  board_tally_add(&tally, 0xC0);
  board_tally_add(&tally, 0xC4);
  STRCMP_EQUAL("0xC0 2, 0xC1 1, 0xC2 1, 0xC3 1, +1 more", format());
}

TEST(BoardTally, return_value_is_the_length_written) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  board_tally_add(&tally, 0xC0);
  const int n = board_tally_format(&tally, buf, sizeof(buf));
  LONGS_EQUAL((long) strlen("primary 1, 0xC0 1"), n);
  STRCMP_EQUAL("primary 1, 0xC0 1", buf);
}

TEST(BoardTally, short_buffer_truncates_but_never_says_none) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  char small[4];
  memset(small, 0xAA, sizeof(small));
  const int n = board_tally_format(&tally, small, sizeof(small));
  LONGS_EQUAL(3, n);
  STRCMP_EQUAL("pri", small);   // snprintf truncated...
  CHECK(small[3] == '\0');      // ...inside the buffer, and not the "none" of an empty tally
}

TEST(BoardTally, formatting_never_writes_past_the_supplied_buffer) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  board_tally_add(&tally, 0xC0);
  board_tally_add(&tally, 0xC1);
  char small[12];
  memset(small, 0xAA, sizeof(small));
  const int n = board_tally_format(&tally, small, sizeof(small));
  LONGS_EQUAL((long) sizeof(small) - 1, n);
  CHECK(small[sizeof(small) - 1] == '\0');
  // What it does write is a prefix of the full line, never a partial token
  // followed by junk.
  STRNCMP_EQUAL("primary 1, 0xC0 1, 0xC1 1", small, 11);
}

TEST(BoardTally, a_zero_length_buffer_writes_nothing) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  LONGS_EQUAL(0, board_tally_format(&tally, buf, 0));
  CHECK(buf[0] == (char) 0xAA);  // untouched
}

/* The one case the format loop cannot reach: it needs room for a character plus a
 * terminator, so a one-byte buffer must still come back terminated. */
TEST(BoardTally, a_one_byte_buffer_is_terminated_and_empty) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  char one[1];
  memset(one, 0xAA, sizeof(one));
  LONGS_EQUAL(0, board_tally_format(&tally, one, sizeof(one)));
  CHECK(one[0] == '\0');
}

/* Widest rendering the type can produce: the primary sentinel at maximum count,
 * three maximum-width physical boards, and the "+65535 more" tail.  This is the
 * contract BOARD_TALLY_BUF_SIZE exists to hold - if the token format or
 * BOARD_TALLY_MAX grows, boards silently drop out of the summary. */
TEST(BoardTally, the_widest_possible_line_still_fits) {
  tally.address[0] = PROBE_ENTRY_DEFAULT_ADDRESS;
  tally.address[1] = 0xC0;
  tally.address[2] = 0xC1;
  tally.address[3] = 0xC2;
  for (int i = 0; i < BOARD_TALLY_MAX; i++) {
    tally.count[i] = 65535U;
  }
  tally.untracked = 65535U;

  const int n = board_tally_format(&tally, buf, sizeof(buf));
  STRCMP_EQUAL("primary 65535, 0xC0 65535, 0xC1 65535, 0xC2 65535, +65535 more", buf);
  LONGS_EQUAL(62, n);
  CHECK(n < (int) sizeof(buf));  // fits with room to spare, not truncated
}

/* The tail is written last, so it is what gets cut when the buffer is short. */
TEST(BoardTally, the_untracked_tail_truncates_inside_the_buffer) {
  board_tally_add(&tally, 0xC0);
  board_tally_add(&tally, 0xC1);
  board_tally_add(&tally, 0xC2);
  board_tally_add(&tally, 0xC3);
  board_tally_add(&tally, 0xC4);  // fifth board -> untracked

  char small[32];
  memset(small, 0xAA, sizeof(small));
  const int n = board_tally_format(&tally, small, sizeof(small));
  LONGS_EQUAL((long) sizeof(small) - 1, n);
  CHECK(small[sizeof(small) - 1] == '\0');
  STRNCMP_EQUAL("0xC0 1, 0xC1 1, 0xC2 1, 0xC3 1,", small, 30);
}
