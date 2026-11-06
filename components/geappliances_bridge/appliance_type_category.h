/*!
 * @file
 * @brief Whether a Home Assistant discovery category applies to an appliance type.
 *
 * Hand-written, but it holds no appliance knowledge of its own: the type ->
 * category fact lives in the generated applianceTypeToErdGroupTranslation[]
 * table (scripts/appliance_type_categories.py is its source of truth). Adding a
 * new appliance type therefore only means updating that Python mapping.
 */
#ifndef APPLIANCE_TYPE_CATEGORY_H
#define APPLIANCE_TYPE_CATEGORY_H

#include <cstdint>
#include <cstring>

#include "erd_lists.h"

// True when `category` (a ha_discovery_categories[].name token) should be
// discovered for `appliance_type` (the value of ERD 0x0008).
//
// Every appliance type gets the common and energy entities, matching
// build_erd_poll_list(), which appends the common and energy ERD lists for
// every type. Types with no category of their own (category == NULL, e.g.
// HomeVentilation, which has no upstream ERDs yet) get nothing else.
static inline bool should_process_category(const char* category, uint8_t appliance_type) {
  if (strcmp(category, "common") == 0 || strcmp(category, "energy") == 0) return true;
  if (appliance_type >= maximumApplianceType) return false;
  const char* type_category = applianceTypeToErdGroupTranslation[appliance_type].category;
  return type_category != nullptr && strcmp(type_category, category) == 0;
}

#endif  // APPLIANCE_TYPE_CATEGORY_H
