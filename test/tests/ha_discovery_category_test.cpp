/*!
 * @file
 * @brief Tests for should_process_category(), the discovery category filter.
 *
 * The filter is driven by the generated applianceTypeToErdGroupTranslation[]
 * table; these tests cover the behavior a bridge owner sees: which sets of
 * Home Assistant entities get created for an appliance type.
 */

#include "appliance_type_category.h"
#include "erd_lists.h"
#include "ha_discovery_data.h"

#include "CppUTest/TestHarness.h"

#include <cstdint>
#include <cstring>

namespace {

bool has_category(const char* name)
{
  for (uint16_t c = 0; c < ha_discovery_category_count; c++) {
    if (strcmp(ha_discovery_categories[c].name, name) == 0) return true;
  }
  return false;
}

bool accepts_anything_else(uint8_t appliance_type)
{
  // Every category the discovery dataset actually ships, so a token added to
  // the data file is covered without touching this test.
  for (uint16_t c = 0; c < ha_discovery_category_count; c++) {
    const char* category = ha_discovery_categories[c].name;
    if (strcmp(category, "custom") == 0 || strcmp(category, "common") == 0 ||
        strcmp(category, "energy") == 0) continue;
    if (should_process_category(category, appliance_type)) return true;
  }
  return false;
}

}  // namespace

TEST_GROUP(ha_discovery_category)
{
  void setup() {}
};

TEST(ha_discovery_category, common_and_energy_apply_to_every_type)
{
  // Includes the Unknown/broadcast value and types the enum has not assigned
  // yet: build_erd_poll_list() appends common + energy ERDs unconditionally,
  // so discovery must publish those entities for every device.
  for (uint16_t type = 0; type <= 255; type++) {
    CHECK_TRUE(should_process_category("common", static_cast<uint8_t>(type)));
    CHECK_TRUE(should_process_category("energy", static_cast<uint8_t>(type)));
  }
}

TEST(ha_discovery_category, common_and_energy_categories_exist_in_the_dataset)
{
  // The filter special-cases these two by name and build_erd_poll_list()
  // appends their ERD lists unconditionally; a rename in the data file would
  // otherwise silently stop energy discovery for every appliance.
  CHECK_TRUE(has_category("common"));
  CHECK_TRUE(has_category("energy"));
}

TEST(ha_discovery_category, type_gets_its_own_category_only)
{
  CHECK_TRUE(should_process_category("dishwasher", 6));
  CHECK_FALSE(should_process_category("laundry", 6));
  CHECK_TRUE(should_process_category("laundry", 2));
  CHECK_FALSE(should_process_category("dishwasher", 2));
  CHECK_TRUE(should_process_category("waterheater", 0));
  CHECK_FALSE(should_process_category("range", 0));
  CHECK_TRUE(should_process_category("airconditioning", 56));
  CHECK_FALSE(should_process_category("refrigeration", 56));
}

TEST(ha_discovery_category, categories_fixed_by_upstream_evidence)
{
  // Microwave used to be discovered as a small appliance and polled the small
  // appliance ERD group; its feature API requires range ERDs.
  CHECK_TRUE(should_process_category("range", 4));
  CHECK_FALSE(should_process_category("smallappliance", 4));
  // ToasterOven and Smoker are small appliances per their ERDs, not ranges.
  CHECK_TRUE(should_process_category("smallappliance", 34));
  CHECK_FALSE(should_process_category("range", 34));
  CHECK_TRUE(should_process_category("smallappliance", 43));
  CHECK_FALSE(should_process_category("range", 43));
  // Dehumidifier polls the air conditioning group; its entities must match.
  CHECK_TRUE(should_process_category("airconditioning", 29));
  CHECK_FALSE(should_process_category("smallappliance", 29));
  // HearthPizzaOven sits in the range group, like PizzaOven.
  CHECK_TRUE(should_process_category("range", 54));
  CHECK_FALSE(should_process_category("smallappliance", 54));
}

TEST(ha_discovery_category, types_without_a_category_get_nothing_else)
{
  // SoftStarter has no upstream ERD and HomeVentilation has none defined yet.
  CHECK_FALSE(accepts_anything_else(53));
  CHECK_FALSE(accepts_anything_else(58));
}

TEST(ha_discovery_category, unknown_appliance_types_get_nothing_else)
{
  // 255 is Unknown/broadcast; 200 is unassigned. Neither may select a category,
  // otherwise a device that has not reported ERD 0x0008 yet would discover a
  // random appliance's entities.
  CHECK_FALSE(accepts_anything_else(255));
  CHECK_FALSE(accepts_anything_else(200));
  CHECK_FALSE(accepts_anything_else(254));
}

TEST(ha_discovery_category, custom_category_is_not_matched_here)
{
  // ha_discovery_manager_run() skips the "custom" category before calling the
  // filter; the filter itself never accepts it.
  CHECK_FALSE(should_process_category("custom", 6));
  CHECK_FALSE(should_process_category("custom", 255));
}
