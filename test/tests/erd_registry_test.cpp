/*!
 * @file
 * @brief Unit tests for ErdRegistry (erd_registry.h / erd_registry.cpp).
 *
 * Tests cover: set_valid_erds, add_valid_erds, is_valid, accessor methods,
 * edge cases (empty set, OOB, capacity).
 */

#include "CppUTest/TestHarness.h"

#include "erd_registry.h"

using namespace esphome::geappliances_bridge;

TEST_GROUP(erd_registry)
{
  ErdRegistry registry;
};

TEST(erd_registry, initial_state)
{
  CHECK_FALSE(registry.has_valid_erds_filter());
  CHECK_EQUAL(0, registry.valid_erd_count());
}

TEST(erd_registry, set_valid_erds_populates_filter)
{
  tiny_erd_t erds[] = { 0x0001, 0x0002, 0x0008 };
  registry.set_valid_erds(erds, 3);

  CHECK_TRUE(registry.has_valid_erds_filter());
  CHECK_EQUAL(3, registry.valid_erd_count());
  CHECK_EQUAL(0x0001, registry.valid_erd(0));
  CHECK_EQUAL(0x0002, registry.valid_erd(1));
  CHECK_EQUAL(0x0008, registry.valid_erd(2));
}

TEST(erd_registry, set_valid_erds_with_null)
{
  registry.set_valid_erds(nullptr, 0);
  CHECK_FALSE(registry.has_valid_erds_filter());
  CHECK_EQUAL(0, registry.valid_erd_count());
}

TEST(erd_registry, set_valid_erds_with_zero_count)
{
  tiny_erd_t erds[] = { 0x0001 };
  registry.set_valid_erds(erds, 0);
  CHECK_FALSE(registry.has_valid_erds_filter());
}

TEST(erd_registry, set_valid_erds_clamps_to_max)
{
  tiny_erd_t erds[700];
  for (int i = 0; i < 700; i++) erds[i] = (tiny_erd_t)i;
  registry.set_valid_erds(erds, 700);

  CHECK_TRUE(registry.has_valid_erds_filter());
  CHECK_EQUAL(ERD_REGISTRY_MAX_VALID, registry.valid_erd_count());
}

TEST(erd_registry, is_valid_without_filter)
{
  CHECK_FALSE(registry.has_valid_erds_filter());
  CHECK_TRUE(registry.is_valid(0x0001));
  CHECK_TRUE(registry.is_valid(0xFFFF));
}

TEST(erd_registry, is_valid_with_filter_matching)
{
  tiny_erd_t erds[] = { 0x0001, 0x0002, 0x0008 };
  registry.set_valid_erds(erds, 3);

  CHECK_TRUE(registry.is_valid(0x0001));
  CHECK_TRUE(registry.is_valid(0x0002));
  CHECK_TRUE(registry.is_valid(0x0008));
}

TEST(erd_registry, is_valid_with_filter_not_matching)
{
  tiny_erd_t erds[] = { 0x0001, 0x0002 };
  registry.set_valid_erds(erds, 2);

  CHECK_FALSE(registry.is_valid(0x0008));
  CHECK_FALSE(registry.is_valid(0xFFFF));
}

TEST(erd_registry, valid_erd_out_of_bounds)
{
  CHECK_EQUAL(0, registry.valid_erd(0));
  CHECK_EQUAL(0, registry.valid_erd(100));
}

TEST(erd_registry, valid_erd_sorted)
{
  tiny_erd_t erds[] = { 0x0100, 0x0050, 0x0200 };
  registry.set_valid_erds(erds, 3);

  CHECK_EQUAL(0x0050, registry.valid_erd(0));
  CHECK_EQUAL(0x0100, registry.valid_erd(1));
  CHECK_EQUAL(0x0200, registry.valid_erd(2));
}

TEST(erd_registry, reinit_valid_erds)
{
  tiny_erd_t erds1[] = { 0x0001, 0x0002 };
  registry.set_valid_erds(erds1, 2);
  CHECK_EQUAL(2, registry.valid_erd_count());

  tiny_erd_t erds2[] = { 0x0008, 0x0009, 0x000A };
  registry.set_valid_erds(erds2, 3);
  CHECK_EQUAL(3, registry.valid_erd_count());
  CHECK_EQUAL(0x0008, registry.valid_erd(0));
}

TEST(erd_registry, is_valid_after_clear_valid_erds)
{
  tiny_erd_t erds[] = { 0x0001 };
  registry.set_valid_erds(erds, 1);
  CHECK_TRUE(registry.is_valid(0x0001));

  // Setting empty set does NOT clear the filter (it's ignored)
  registry.set_valid_erds(nullptr, 0);
  CHECK_TRUE(registry.has_valid_erds_filter());
  CHECK_TRUE(registry.is_valid(0x0001));
}

/* ------------------------------------------------------------------ */
/* add_valid_erds                                                     */
/* ------------------------------------------------------------------ */

TEST(erd_registry, add_valid_erds_appends_to_existing_set)
{
  uint16_t base[] = {0x0001, 0x0003, 0x0005};
  registry.set_valid_erds(base, 3);

  uint16_t extra[] = {0x0002, 0x0004};
  registry.add_valid_erds(extra, 2);

  CHECK_EQUAL(5, registry.valid_erd_count());
  CHECK_TRUE(registry.is_valid(0x0001));
  CHECK_TRUE(registry.is_valid(0x0002));
  CHECK_TRUE(registry.is_valid(0x0003));
  CHECK_TRUE(registry.is_valid(0x0004));
  CHECK_TRUE(registry.is_valid(0x0005));
  CHECK_FALSE(registry.is_valid(0x0006));
}

TEST(erd_registry, add_valid_erds_deduplicates)
{
  uint16_t base[] = {0x0001, 0x0002, 0x0003};
  registry.set_valid_erds(base, 3);

  uint16_t extra[] = {0x0002, 0x0004};  // 0x0002 already in base
  registry.add_valid_erds(extra, 2);

  CHECK_EQUAL(4, registry.valid_erd_count());
  CHECK_TRUE(registry.is_valid(0x0002));
  CHECK_TRUE(registry.is_valid(0x0004));
}

TEST(erd_registry, add_valid_erds_no_op_without_set_valid_erds)
{
  uint16_t extra[] = {0x0001, 0x0002};
  registry.add_valid_erds(extra, 2);

  // Filter should still be inactive
  CHECK_FALSE(registry.has_valid_erds_filter());
  CHECK_EQUAL(0, registry.valid_erd_count());
}

TEST(erd_registry, add_valid_erds_remains_sorted)
{
  uint16_t base[] = {0x0005, 0x0001, 0x0003};
  registry.set_valid_erds(base, 3);

  uint16_t extra[] = {0x0002, 0x0006, 0x0004};
  registry.add_valid_erds(extra, 3);

  // Verify sorted order
  CHECK_EQUAL(0x0001, registry.valid_erd(0));
  CHECK_EQUAL(0x0002, registry.valid_erd(1));
  CHECK_EQUAL(0x0003, registry.valid_erd(2));
  CHECK_EQUAL(0x0004, registry.valid_erd(3));
  CHECK_EQUAL(0x0005, registry.valid_erd(4));
  CHECK_EQUAL(0x0006, registry.valid_erd(5));
}

TEST(erd_registry, add_valid_erds_with_null)
{
  uint16_t base[] = {0x0001};
  registry.set_valid_erds(base, 1);

  registry.add_valid_erds(nullptr, 0);

  CHECK_EQUAL(1, registry.valid_erd_count());
}

TEST(erd_registry, add_valid_erds_deduplicates_within_batch)
{
  uint16_t base[] = {0x0001, 0x0002};
  registry.set_valid_erds(base, 2);

  uint16_t extra[] = {0x0003, 0x0003, 0x0004, 0x0003};  // 0x0003 repeated
  registry.add_valid_erds(extra, 4);

  CHECK_EQUAL(4, registry.valid_erd_count());
  CHECK_TRUE(registry.is_valid(0x0003));
  CHECK_TRUE(registry.is_valid(0x0004));
}

TEST(erd_registry, add_valid_erds_respects_capacity)
{
  // Fill registry to capacity with set_valid_erds.
  uint16_t base[ERD_REGISTRY_MAX_VALID];
  for (uint16_t i = 0; i < ERD_REGISTRY_MAX_VALID; i++) {
    base[i] = i;
  }
  registry.set_valid_erds(base, ERD_REGISTRY_MAX_VALID);

  CHECK_EQUAL(ERD_REGISTRY_MAX_VALID, registry.valid_erd_count());

  // Try to add more ERDs — should be silently rejected.
  uint16_t extra[] = {0xFFFF, 0xFFFE, 0xFFFD};
  registry.add_valid_erds(extra, 3);

  CHECK_EQUAL(ERD_REGISTRY_MAX_VALID, registry.valid_erd_count());
  // Original entries still valid.
  CHECK_TRUE(registry.is_valid(0x0000));
  CHECK_TRUE(registry.is_valid(ERD_REGISTRY_MAX_VALID - 1));
  // New entries not added.
  CHECK_FALSE(registry.is_valid(0xFFFF));
}


/* ------------------------------------------------------------------ */
/* rec 9 borrow semantics                                              */
/* ------------------------------------------------------------------ */

TEST(erd_registry, set_valid_erds_sorts_the_callers_array_in_place)
{
  // The rec 9 discriminator: the registry no longer keeps its own copy, so
  // the caller's array itself must come out sorted. Under the pre-rec 9
  // copy implementation the caller's array keeps its original (scrambled)
  // order and this test fails.
  tiny_erd_t erds[] = { 0x0200, 0x0050, 0x0100, 0x0010 };
  registry.set_valid_erds(erds, 4);

  CHECK_EQUAL(0x0010, erds[0]);
  CHECK_EQUAL(0x0050, erds[1]);
  CHECK_EQUAL(0x0100, erds[2]);
  CHECK_EQUAL(0x0200, erds[3]);
}

TEST(erd_registry, re_set_valid_erds_drops_previous_extras)
{
  tiny_erd_t base[] = { 0x0001, 0x0002 };
  registry.set_valid_erds(base, 2);

  tiny_erd_t extra[] = { 0x0F00 };
  registry.add_valid_erds(extra, 1);
  CHECK_EQUAL(3, registry.valid_erd_count());
  CHECK_TRUE(registry.is_valid(0x0F00));

  // Re-pointing the base set starts a fresh merged set: the extras from the
  // previous base set are gone.
  tiny_erd_t base2[] = { 0x0008, 0x0009 };
  registry.set_valid_erds(base2, 2);
  CHECK_EQUAL(2, registry.valid_erd_count());
  CHECK_FALSE(registry.is_valid(0x0F00));
}

TEST(erd_registry, valid_erd_merges_interleaved_extras)
{
  // A custom ERD smaller than some base ERDs must land in the merged sorted
  // view, not appended past the end.
  tiny_erd_t base[] = { 0x0100, 0x0300 };
  registry.set_valid_erds(base, 2);

  tiny_erd_t extra[] = { 0x0200 };
  registry.add_valid_erds(extra, 1);

  CHECK_EQUAL(3, registry.valid_erd_count());
  CHECK_EQUAL(0x0100, registry.valid_erd(0));
  CHECK_EQUAL(0x0200, registry.valid_erd(1));
  CHECK_EQUAL(0x0300, registry.valid_erd(2));
}

TEST(erd_registry, add_valid_erds_deduplicates_across_batches)
{
  // A custom ERD that duplicates a base-set entry must not land in the
  // registry's own storage: the sets stay disjoint, and the merged view
  // reports the base entry once.
  tiny_erd_t base[] = { 0x0100, 0x0300 };
  registry.set_valid_erds(base, 2);

  tiny_erd_t extra[] = { 0x0300, 0x0400 };
  registry.add_valid_erds(extra, 2);

  CHECK_EQUAL(3, registry.valid_erd_count());
  CHECK_TRUE(registry.is_valid(0x0300));
  CHECK_TRUE(registry.is_valid(0x0400));
  CHECK_EQUAL(0x0300, registry.valid_erd(1));
  CHECK_EQUAL(0x0400, registry.valid_erd(2));
}

TEST(erd_registry, add_valid_erds_deduplicates_against_its_own_extras)
{
  /* The linear scan over the registry's own extra storage is what catches a
   * duplicate of a previously-added extra and a duplicate within one batch.
   * Neither lives in the base set, so the binary-search branch cannot catch
   * them: call 1 adds 0x0500, call 2 passes {0x0500, 0x0600, 0x0600}. */
  tiny_erd_t base[] = { 0x0100 };
  registry.set_valid_erds(base, 1);

  tiny_erd_t first[] = { 0x0500 };
  registry.add_valid_erds(first, 1);

  tiny_erd_t second[] = { 0x0500, 0x0600, 0x0600 };
  registry.add_valid_erds(second, 3);

  CHECK_EQUAL(3, registry.valid_erd_count());
  CHECK_TRUE(registry.is_valid(0x0500));
  CHECK_TRUE(registry.is_valid(0x0600));
  CHECK_EQUAL(0x0100, registry.valid_erd(0));
  CHECK_EQUAL(0x0500, registry.valid_erd(1));
  CHECK_EQUAL(0x0600, registry.valid_erd(2));
}

TEST(erd_registry, clear_valid_erds_disables_the_filter)
{
  tiny_erd_t erds[] = {0x0001, 0x0002};
  registry.set_valid_erds(erds, 2);

  registry.clear_valid_erds();

  CHECK_FALSE(registry.has_valid_erds_filter());
  CHECK_EQUAL(0, registry.valid_erd_count());
  CHECK_TRUE(registry.is_valid(0x1234));
}

TEST(erd_registry, set_valid_erds_after_clear_installs_the_new_set)
{
  tiny_erd_t old_erds[] = {0x0001};
  tiny_erd_t new_erds[] = {0x0002, 0x0003};
  registry.set_valid_erds(old_erds, 1);
  registry.clear_valid_erds();

  registry.set_valid_erds(new_erds, 2);

  CHECK_FALSE(registry.is_valid(0x0001));
  CHECK_TRUE(registry.is_valid(0x0002));
}
