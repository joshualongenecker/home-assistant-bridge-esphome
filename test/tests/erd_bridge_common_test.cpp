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
 * nothing, so the wording and the suppression rules are asserted on the rendered
 * string. */

static polled_erds_state_t s_state;  // storage so the helper can hand back a pointer

static const polled_erds_state_t* make_state(bool polling_initialized,
                                             uint16_t custom_count, uint16_t unseen_count) {
  s_state.polling_bridge_initialized = polling_initialized;
  s_state.custom_erds_count = custom_count;
  s_state.unseen_custom_count = unseen_count;
  return &s_state;
}

TEST(BoardTally, summary_splits_the_polled_count_by_board) {
  const uint8_t polled[] = {PROBE_ENTRY_DEFAULT_ADDRESS, PROBE_ENTRY_DEFAULT_ADDRESS, 0xC0};
  board_polled_summary_format(make_state(true, 65, 3), polled, 3, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: 3 (primary 2, 0xC0 1)", buf);
}

/* Regression: the split must describe the set the count describes.  The probe
 * phase drops every ERD whose read failed, so the list the bridge was BUILT from
 * is larger than what it polls (gea-esphome-refer: 60 built, 53 polled).  Here 50
 * custom ERDs were configured but only 48 are polled, and the split sums to 48. */
TEST(BoardTally, summary_splits_the_polled_set_not_the_list_it_was_built_from) {
  uint8_t polled[48];
  memset(polled, PROBE_ENTRY_DEFAULT_ADDRESS, 40);
  memset(polled + 40, 0xC0, 8);
  board_polled_summary_format(make_state(true, 50, 50), polled, 48, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: 48 (primary 40, 0xC0 8)", buf);
}

TEST(BoardTally, summary_stays_quiet_on_a_single_board_appliance) {
  // "Entered steady-state polling: N ERDs" already gives the count at INFO, and a
  // one-board split adds nothing to it.
  uint8_t polled[53];
  memset(polled, PROBE_ENTRY_DEFAULT_ADDRESS, sizeof(polled));
  LONGS_EQUAL(0, board_polled_summary_format(make_state(true, 0, 0), polled, 53,
                                             buf, sizeof(buf)));
  STRCMP_EQUAL("", buf);
}

TEST(BoardTally, summary_stays_quiet_before_the_probe_phase_fills_the_list) {
  LONGS_EQUAL(0, board_polled_summary_format(make_state(true, 65, 3), NULL, 0,
                                             buf, sizeof(buf)));
  STRCMP_EQUAL("", buf);
}

TEST(BoardTally, summary_keeps_board_zero_distinct_from_the_primary_sentinel) {
  const uint8_t polled[] = {0x00, 0x00, PROBE_ENTRY_DEFAULT_ADDRESS};
  board_polled_summary_format(make_state(true, 0, 0), polled, 3, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: 3 (0x00 2, primary 1)", buf);
}

TEST(BoardTally, summary_collapses_boards_past_four_into_the_untracked_tail) {
  const uint8_t polled[] = {0xC0, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4};
  board_polled_summary_format(make_state(true, 0, 0), polled, 6, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: 6 (0xC0 2, 0xC1 1, 0xC2 1, 0xC3 1, +1 more)", buf);
}

TEST(BoardTally, summary_truncates_inside_the_buffer) {
  const uint8_t polled[] = {0xC0, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4};
  char small[24];
  memset(small, 0xAA, sizeof(small));
  const int n = board_polled_summary_format(make_state(true, 0, 0), polled, 6,
                                            small, sizeof(small));
  LONGS_EQUAL((long) sizeof(small) - 1, n);  // stored length, not the would-be length
  CHECK(small[sizeof(small) - 1] == '\0');
  STRNCMP_EQUAL("Polled ERDs: 6 (0xC0 2,", small, 23);
}

/* The three "nothing is polled, because ..." branches share the same clamp.
 * snprintf reports the length it WOULD have written and the caller only checks
 * "> 0", so an unclamped return would log a truncated line as though it were
 * whole - the same contract the tally formatter had to learn the hard way. */
TEST(BoardTally, summary_reports_the_stored_length_for_a_truncated_reason_line) {
  char small[8];
  memset(small, 0xAA, sizeof(small));
  const int n = board_polled_summary_format(make_state(false, 0, 0), NULL, 0,
                                            small, sizeof(small));
  LONGS_EQUAL((long) sizeof(small) - 1, n);  // the full line is 45 characters
  CHECK(small[sizeof(small) - 1] == '\0');
  STRNCMP_EQUAL("Polled ", small, 7);
}

TEST(BoardTally, summary_reports_the_stored_length_for_a_one_byte_reason_buffer) {
  char one[1];
  one[0] = (char) 0xAA;
  const int n = board_polled_summary_format(make_state(false, 65, 7), NULL, 0, one, sizeof(one));
  LONGS_EQUAL(0, n);
  CHECK(one[0] == '\0');  // armed up front, so the buffer is never left unterminated
}


TEST(BoardTally, summary_names_no_custom_erds_rather_than_a_subscription_reason) {
  board_polled_summary_format(make_state(false, 0, 0), NULL, 0, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: none - no custom ERDs configured", buf);
}

TEST(BoardTally, summary_credits_subscription_only_when_it_covers_every_custom_erd) {
  board_polled_summary_format(make_state(false, 65, 0), NULL, 0, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: none - custom ERDs all arrive by subscription", buf);
}

TEST(BoardTally, summary_counts_uncovered_erds_when_no_bridge_polls_them) {
  board_polled_summary_format(make_state(false, 65, 7), NULL, 0, buf, sizeof(buf));
  STRCMP_EQUAL("Polled ERDs: none - 7 custom ERDs uncovered, no polling bridge", buf);
}
