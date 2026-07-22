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
  /* If the valid set hasn't been initialized, nothing to append to. */
  if (!valid_erds_ready_ || !erds || count == 0) {
    return;
  }
  /* Save the original sorted count. New entries are appended unsorted;
   * binary_search must only search the pre-existing sorted prefix. */
  uint16_t base_count = valid_erds_count_;
  for (uint16_t i = 0; i < count; i++) {
    /* Deduplicate against existing sorted entries (only the base prefix). */
    if (std::binary_search(valid_erds_, valid_erds_ + base_count, erds[i])) {
      continue;
    }
    /* Deduplicate within the input batch. */
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

void ErdRegistry::clear_registered_erds()
{
  registered_erds_count_ = 0;
}

void ErdRegistry::register_erd(tiny_erd_t erd)
{
  /* Deduplicate. */
  for (uint16_t i = 0; i < registered_erds_count_; i++) {
    if (registered_erds_[i] == erd) return;
  }
  if (registered_erds_count_ >= ERD_REGISTRY_MAX_VALID) return;
  registered_erds_[registered_erds_count_++] = erd;
}

bool ErdRegistry::is_valid(tiny_erd_t erd) const
{
  if (!valid_erds_ready_) {
    return true;  /* No filter active: all ERDs are valid. */
  }
  /* Binary search in sorted array. */
  return std::binary_search(valid_erds_, valid_erds_ + valid_erds_count_, erd);
}

tiny_erd_t ErdRegistry::registered_erd(uint16_t idx) const
{
  if (idx >= registered_erds_count_) return 0;
  return registered_erds_[idx];
}

tiny_erd_t ErdRegistry::valid_erd(uint16_t idx) const
{
  if (idx >= valid_erds_count_) return 0;
  return valid_erds_[idx];
}

}  // namespace geappliances_bridge
}  // namespace esphome
