#include "erd_registry.h"
#include <algorithm>

namespace esphome {
namespace geappliances_bridge {

void ErdRegistry::set_valid_erds(const tiny_erd_t* erds, uint16_t count)
{
  /* An empty set would silently suppress all publishes; ignore it so filtering
   * stays disabled and all ERDs continue to be published. */
  if (!erds || count == 0) {
    return;
  }
  uint16_t n = (count > ERD_REGISTRY_MAX_VALID) ? ERD_REGISTRY_MAX_VALID : count;
  for (uint16_t i = 0; i < n; i++) {
    valid_erds_[i] = erds[i];
  }
  valid_erds_count_ = n;
  /* Sort for binary search in is_valid(). */
  std::sort(valid_erds_, valid_erds_ + n);
  valid_erds_ready_ = true;
}

void ErdRegistry::add_valid_erds(const tiny_erd_t* erds, uint16_t count)
{
  if (!valid_erds_ready_ || !erds || count == 0) {
    return;
  }
  /* Save the original sorted count. New entries are appended unsorted;
   * binary_search must only search the pre-existing sorted prefix. */
  uint16_t base_count = valid_erds_count_;
  for (uint16_t i = 0; i < count; i++) {
    if (std::binary_search(valid_erds_, valid_erds_ + base_count, erds[i])) {
      continue;
    }
    bool dup = false;
    for (uint16_t k = 0; k < i; k++) {
      if (erds[k] == erds[i]) { dup = true; break; }
    }
    if (dup) continue;
    if (valid_erds_count_ >= ERD_REGISTRY_MAX_VALID) {
      break;
    }
    valid_erds_[valid_erds_count_++] = erds[i];
  }
  /* Re-sort to maintain binary search correctness. */
  std::sort(valid_erds_, valid_erds_ + valid_erds_count_);
}

bool ErdRegistry::is_valid(tiny_erd_t erd) const
{
  if (!valid_erds_ready_) {
    return true;  /* No filter active: all ERDs are valid. */
  }
  return std::binary_search(valid_erds_, valid_erds_ + valid_erds_count_, erd);
}

tiny_erd_t ErdRegistry::valid_erd(uint16_t idx) const
{
  if (idx >= valid_erds_count_) return 0;
  return valid_erds_[idx];
}

}  // namespace geappliances_bridge
}  // namespace esphome
