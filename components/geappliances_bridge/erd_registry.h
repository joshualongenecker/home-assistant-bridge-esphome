
#pragma once
#include <cstdint>

#include "feature_bit_manager.h"

extern "C" {
#include "tiny_erd.h"
}

namespace esphome {
namespace geappliances_bridge {

/* Maximum number of ERDs the borrowed valid set can hold. Sized to
 * FEATURE_BIT_MAX_ERDS: the feature-bit manager's array is the only base set. */
#define ERD_REGISTRY_MAX_VALID 645

/* The base set is truncated at ERD_REGISTRY_MAX_VALID by set_valid_erds();
 * entries past it would be silently suppressed by is_valid(). */
static_assert(ERD_REGISTRY_MAX_VALID >= FEATURE_BIT_MAX_ERDS,
              "ERD_REGISTRY_MAX_VALID must cover the whole FeatureBitManager array");

/* Maximum number of ERDs the registry stores itself — the custom ERDs merged
 * in by add_valid_erds(). Pinned to GeappliancesBridge::CUSTOM_ERDS_MAX by a
 * static_assert in geappliances_bridge.h. */
#define ERD_REGISTRY_MAX_EXTRA 128

class ErdRegistry {
 public:
  // -------------------------------------------------------------------------
  // Setup methods (called during bridge initialization)
  // -------------------------------------------------------------------------

  /// Enable valid-ERD filtering from the caller's array — no copy. The array
  /// is sorted in place (the registry is the only consumer that needs the
  /// order) and then borrowed, so the caller must keep it alive while the
  /// filter is active: FeatureBitManager's array is a fixed member array, so
  /// it outlives the registry. Architecture review rec 9 removed the
  /// registry's own 645-entry copy of the same set.
  /// An empty set is ignored so filtering stays disabled.
  void set_valid_erds(tiny_erd_t* erds, uint16_t count);

  /// Deduplicates against the borrowed set and against earlier additions. At
  /// most ERD_REGISTRY_MAX_EXTRA entries get their own storage, and the merged
  /// set never exceeds ERD_REGISTRY_MAX_VALID; anything past that is dropped.
  /// If the valid set has not been initialized by set_valid_erds(), this
  /// call is a no-op (filtering stays disabled).
  void add_valid_erds(const tiny_erd_t* erds, uint16_t count);

  // -------------------------------------------------------------------------
  // Runtime methods (called by the subscription bridge during operation)
  // -------------------------------------------------------------------------

  /// Returns true if valid-ERD filtering is active.
  /// When false, all ERDs pass the filter.
  bool has_valid_erds_filter() const { return valid_erds_ready_; }

  /// Returns true if the ERD passes the valid-ERD filter (or no filter active).
  /// Two binary searches — one over the borrowed set, one over the registry's
  /// own additions — instead of the single search a merged copy would allow.
  bool is_valid(tiny_erd_t erd) const;

  // -------------------------------------------------------------------------
  // Read-only accessors (diagnostics)
  // -------------------------------------------------------------------------

  /// Returns the number of valid ERDs (filter).
  uint16_t valid_erd_count() const { return base_count_ + extra_count_; }

  /// Returns the valid ERD at the given index (0-based) in the merged sorted
  /// order of both sets. O(n) walk, diagnostics only.
  tiny_erd_t valid_erd(uint16_t idx) const;

 private:
  // The base set, borrowed from FeatureBitManager and sorted by
  // set_valid_erds(). Never owned here.
  const tiny_erd_t* base_erds_{nullptr};
  uint16_t base_count_{0};

  // Entries the registry owns (custom ERDs), kept sorted and disjoint from the
  // base set by add_valid_erds().
  tiny_erd_t extra_erds_[ERD_REGISTRY_MAX_EXTRA];
  uint16_t extra_count_{0};

  bool valid_erds_ready_{false};
};

}  // namespace geappliances_bridge
}  // namespace esphome
