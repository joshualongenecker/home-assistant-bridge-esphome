#pragma once
#include <cstdint>

#include "feature_bit_manager.h"

extern "C" {
#include "tiny_erd.h"
}

namespace esphome {
namespace geappliances_bridge {

#define ERD_REGISTRY_MAX_VALID 645

static_assert(ERD_REGISTRY_MAX_VALID >= FEATURE_BIT_MAX_ERDS,
              "ERD_REGISTRY_MAX_VALID must cover the whole FeatureBitManager array");

#define ERD_REGISTRY_MAX_EXTRA 128

class ErdRegistry {
 public:
  // -------------------------------------------------------------------------
  // Setup methods (called during bridge initialization)
  // -------------------------------------------------------------------------

  void set_valid_erds(tiny_erd_t* erds, uint16_t count);

  void add_valid_erds(const tiny_erd_t* erds, uint16_t count);

  void clear_valid_erds() {
    base_erds_ = nullptr;
    base_count_ = 0;
    extra_count_ = 0;
    valid_erds_ready_ = false;
  }

  // -------------------------------------------------------------------------
  // Runtime methods (called by the subscription bridge during operation)
  // -------------------------------------------------------------------------

  bool has_valid_erds_filter() const { return valid_erds_ready_; }

  bool is_valid(tiny_erd_t erd) const;

  // -------------------------------------------------------------------------
  // Read-only accessors (diagnostics)
  // -------------------------------------------------------------------------

  uint16_t valid_erd_count() const { return base_count_ + extra_count_; }

  tiny_erd_t valid_erd(uint16_t idx) const;

 private:
  const tiny_erd_t* base_erds_{nullptr};
  uint16_t base_count_{0};

  tiny_erd_t extra_erds_[ERD_REGISTRY_MAX_EXTRA];
  uint16_t extra_count_{0};

  bool valid_erds_ready_{false};
};

}
}
