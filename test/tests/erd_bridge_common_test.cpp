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

/* ---------- the "Polled ERDs" summary line ----------
 * The line exists only inside an ESP_LOGD, which this harness compiles to
 * nothing, so the wording and the two suppression rules are asserted on the
 * rendered string. */

static polled_erds_state_t s_state;  // storage so the helper can hand back a pointer

static const polled_erds_state_t* make_state(bool polling_initialized,
                                             bool custom_polling_started,
                                             uint16_t polled_count, uint16_t custom_count,
                                             uint16_t unseen_count) {
  s_state.polling_bridge_initialized = polling_initialized;
  s_state.custom_erd_polling_started = custom_polling_started;
  s_state.polled_count = polled_count;
  s_state.custom_erds_count = custom_count;
  s_state.unseen_custom_count = unseen_count;
  return &s_state;
}

TEST(BoardTally, summary_reports_the_per_board_split_while_custom_polling_runs) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  board_tally_add(&tally, 0xC0);
  board_polled_summary_format(make_state(true, true, 53, 65, 3), &tally, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: 53 (custom: primary 2, 0xC0 1)", buf);
}

TEST(BoardTally, summary_stays_quiet_when_the_bridge_already_reported_the_count) {
  // "Entered steady-state polling: N ERDs" logs the same polling_list_count at
  // INFO on the same transition, so a DEBUG repeat adds nothing.
  LONGS_EQUAL(0, board_polled_summary_format(
                    make_state(true, false, 53, 65, 0), NULL, buf, sizeof(buf)));
  STRCMP_EQUAL("", buf);
}

TEST(BoardTally, summary_stays_quiet_when_there_is_no_split_to_add) {
  board_tally_add(&tally, 0xC0);
  LONGS_EQUAL(0, board_polled_summary_format(
                    make_state(true, true, 53, 65, 0), &tally, buf, sizeof(buf)));
  STRCMP_EQUAL("", buf);
}

/* Regression: after handle_subscription_failed() rebuilds the polling bridge from
 * the full custom list, subscription_unseen_custom_erds_ is a stale subset whose
 * numbers no longer sum to the count. The tally must not ride beside it. */
TEST(BoardTally, summary_never_attaches_a_stale_tally_after_a_subscription_failure) {
  board_tally_add(&tally, PROBE_ENTRY_DEFAULT_ADDRESS);
  board_tally_add(&tally, 0xC0);
  LONGS_EQUAL(0, board_polled_summary_format(
                    make_state(true, false, 151, 65, 2), &tally, buf, sizeof(buf)));
  STRCMP_EQUAL("", buf);
}

TEST(BoardTally, summary_tolerates_a_missing_tally) {
  LONGS_EQUAL(0, board_polled_summary_format(
                    make_state(true, true, 53, 65, 3), NULL, buf, sizeof(buf)));
  STRCMP_EQUAL("", buf);
}

TEST(BoardTally, summary_names_no_custom_erds_rather_than_a_subscription_reason) {
  board_polled_summary_format(make_state(false, false, 0, 0, 0), NULL, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: none - no custom ERDs configured", buf);
}

TEST(BoardTally, summary_credits_subscription_only_when_it_covers_every_custom_erd) {
  board_polled_summary_format(make_state(false, false, 0, 65, 0), NULL, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: none - custom ERDs all arrive by subscription", buf);
}

TEST(BoardTally, summary_counts_uncovered_erds_when_no_bridge_polls_them) {
  board_polled_summary_format(make_state(false, false, 0, 65, 7), NULL, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: none - 7 custom ERDs uncovered, no polling bridge", buf);
}

TEST(BoardTally, summary_fits_its_declared_buffer) {
  // Widest line: five-digit count plus a full-width tally.
  for (int i = 0; i < BOARD_TALLY_MAX; i++) {
    tally.address[i] = (i == 0) ? PROBE_ENTRY_DEFAULT_ADDRESS : (uint8_t) (0xC0 + i);
    tally.count[i] = 65535U;
  }
  tally.untracked = 65535U;
  char line[BOARD_POLLED_SUMMARY_BUF_SIZE];
  const int n = board_polled_summary_format(
      make_state(true, true, 65535, 65535, 65535), &tally, line, sizeof(line));
  CHECK(n < (int) sizeof(line));  // not truncated
  STRCMP_EQUAL("Polled ERDs: 65535 (custom: primary 65535, 0xC1 65535, 0xC2 65535, "
               "0xC3 65535, +65535 more)", line);
}
