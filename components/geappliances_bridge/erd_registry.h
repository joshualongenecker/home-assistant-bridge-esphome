
#pragma once

#include <cstdint>

extern "C" {
#include "tiny_erd.h"
}

namespace esphome {
namespace geappliances_bridge {

/* Maximum number of valid ERDs the registry can track. */
#define ERD_REGISTRY_MAX_VALID 645

class ErdRegistry {
 public:
  // -------------------------------------------------------------------------
  // Setup methods (called during bridge initialization)
  // -------------------------------------------------------------------------

  /// Copy the valid-ERD set from FeatureBitManager and enable valid-ERD
  /// filtering. An empty set is ignored so filtering stays disabled.
  void set_valid_erds(const tiny_erd_t* erds, uint16_t count);

  /// Append additional ERDs to the valid set (e.g., custom ERDs).
  /// Deduplicates against existing entries and re-sorts.
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
  bool is_valid(tiny_erd_t erd) const;

  // -------------------------------------------------------------------------
  // Read-only accessors (diagnostics)
  // -------------------------------------------------------------------------

  /// Returns the number of valid ERDs (filter).
  uint16_t valid_erd_count() const { return valid_erds_count_; }

  /// Returns the valid ERD at the given index (0-based).
  tiny_erd_t valid_erd(uint16_t idx) const;

 private:
  tiny_erd_t valid_erds_[ERD_REGISTRY_MAX_VALID];
  uint16_t valid_erds_count_{0};
  bool valid_erds_ready_{false};
};

}  // namespace geappliances_bridge
}  // namespace esphome
