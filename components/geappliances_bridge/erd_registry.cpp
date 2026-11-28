#include "erd_registry.h"
#include <algorithm>

namespace esphome {
namespace geappliances_bridge {

void ErdRegistry::set_valid_erds(tiny_erd_t* erds, uint16_t count)
{
  if (!erds || count == 0) {
    return;
  }
  uint16_t n = (count > ERD_REGISTRY_MAX_VALID) ? ERD_REGISTRY_MAX_VALID : count;
  std::sort(erds, erds + n);
  base_erds_ = erds;
  base_count_ = n;
  extra_count_ = 0;
  valid_erds_ready_ = true;
}

void ErdRegistry::add_valid_erds(const tiny_erd_t* erds, uint16_t count)
{
  if (!valid_erds_ready_ || !erds || count == 0) {
    return;
  }
  for (uint16_t i = 0; i < count; i++) {
    if (std::binary_search(base_erds_, base_erds_ + base_count_, erds[i])) {
      continue;
    }
    bool dup = false;
    for (uint16_t k = 0; k < extra_count_; k++) {
      if (extra_erds_[k] == erds[i]) { dup = true; break; }
    }
    if (dup) continue;
    if (extra_count_ >= ERD_REGISTRY_MAX_EXTRA ||
        base_count_ + extra_count_ >= ERD_REGISTRY_MAX_VALID) {
      break;
    }
    extra_erds_[extra_count_++] = erds[i];
  }
  std::sort(extra_erds_, extra_erds_ + extra_count_);
}

bool ErdRegistry::is_valid(tiny_erd_t erd) const
{
  if (!valid_erds_ready_) {
    return true;
  }
  return std::binary_search(base_erds_, base_erds_ + base_count_, erd) ||
         std::binary_search(extra_erds_, extra_erds_ + extra_count_, erd);
}

tiny_erd_t ErdRegistry::valid_erd(uint16_t idx) const
{
  const uint16_t total = base_count_ + extra_count_;
  if (idx >= total) return 0;
  uint16_t b = 0;
  uint16_t e = 0;
  for (uint16_t i = 0; i < total; i++) {
    const bool take_base = (b < base_count_) &&
                           (e >= extra_count_ || base_erds_[b] < extra_erds_[e]);
    if (take_base) {
      if (i == idx) return base_erds_[b];
      b++;
    } else {
      if (i == idx) return extra_erds_[e];
      e++;
    }
  }
  return 0;
}

}
}
