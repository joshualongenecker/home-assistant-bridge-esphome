/*!
 * @file
 * @brief Tests for the generated appliance-type -> ERD group table.
 *
 * The table is the single source of truth for which ERD group and which
 * discovery category an appliance type belongs to, so these tests pin the
 * table's structural contract and the category assignments that upstream
 * evidence settled (see scripts/appliance_type_categories.py).
 */

#include "erd_lists.h"
#include "ha_discovery_data.h"

#include "CppUTest/TestHarness.h"

#include <cstring>

namespace {

struct category_arrays {
  const char* name;
  const tiny_erd_t* erds;
  uint16_t count;
};

// The ten ERD range groups, by discovery category token.
const category_arrays kCategories[] = {
  {"common", commonErds, commonErdCount},
  {"refrigeration", refrigerationErds, refrigerationErdCount},
  {"laundry", laundryErds, laundryErdCount},
  {"dishwasher", dishWasherErds, dishWasherErdCount},
  {"waterheater", waterHeaterErds, waterHeaterErdCount},
  {"range", rangeErds, rangeErdCount},
  {"airconditioning", airConditioningErds, airConditioningErdCount},
  {"waterfilter", waterFilterErds, waterFilterErdCount},
  {"smallappliance", smallApplianceErds, smallApplianceErdCount},
  {"energy", energyErds, energyErdCount},
};

const category_arrays* find_category(const char* name)
{
  for (const auto& c : kCategories) {
    if (strcmp(c.name, name) == 0) return &c;
  }
  return nullptr;
}

// Appliance types whose category was decided against the older hand-written
// mapping, with the upstream evidence that decided it.
struct expected_category {
  uint8_t type;
  const char* category;  // nullptr = no category-specific ERDs upstream
};

const expected_category kEvidenceBacked[] = {
  {4, "range"},           // Microwave: feature API requires 67 range ERDs
  {18, "refrigeration"},  // DeliveryBox: 5 refrigeration vs 2 laundry ERDs
  {28, "refrigeration"},  // InHomeGrower: 52 refrigeration ERDs
  {29, "airconditioning"},// Dehumidifier: 16 airconditioning ERDs
  {30, "smallappliance"}, // UnderCounterIceMaker: 13 smallappliance ERDs
  {34, "smallappliance"}, // ToasterOven: 112 smallappliance vs 6 range ERDs
  {43, "smallappliance"}, // Smoker: 65 smallappliance vs 1 range ERD
  {53, nullptr},          // SoftStarter: no upstream ERD names it
  {54, "range"},          // HearthPizzaOven: old table was off by one slot
  {55, "smallappliance"}, // SourdoughStarter: ERDs 0x9500, 0x9505
  {56, "airconditioning"},// Thermostat: was missing from the old table
  {57, "airconditioning"},// AirPurifier: ERDs 0x7051, 0x7053
  {58, nullptr},          // HomeVentilation: no upstream ERD defined yet
};

}  // namespace

TEST_GROUP(erd_lists)
{
  void setup() {}
};

TEST(erd_lists, group_entry_pairs_a_category_with_its_own_erd_list)
{
  for (uint16_t i = 0; i < maximumApplianceType; i++) {
    const applianceTypeToErdListAndCount_t& group = applianceTypeToErdGroupTranslation[i];
    if (group.category == nullptr) {
      CHECK_TRUE(group.erdList == nullptr);
      CHECK_EQUAL(0u, group.erdCount);
      continue;
    }
    const category_arrays* arrays = find_category(group.category);
    // A token naming no ERD array would discover entities for ERDs that are
    // never polled.
    CHECK_TRUE(arrays != nullptr);
    if (arrays == nullptr) continue;
    CHECK_TRUE(group.erdList == arrays->erds);
    CHECK_EQUAL(arrays->count, group.erdCount);
  }
}

TEST(erd_lists, every_group_category_has_discovery_entities)
{
  for (uint16_t i = 0; i < maximumApplianceType; i++) {
    const char* category = applianceTypeToErdGroupTranslation[i].category;
    if (category == nullptr) continue;
    bool found = false;
    for (uint16_t c = 0; c < ha_discovery_category_count; c++) {
      if (strcmp(ha_discovery_categories[c].name, category) == 0) {
        found = true;
        break;
      }
    }
    CHECK_TRUE(found);
  }
}

TEST(erd_lists, evidence_backed_categories)
{
  for (const auto& expected : kEvidenceBacked) {
    CHECK_TRUE(expected.type < maximumApplianceType);
    const char* actual = applianceTypeToErdGroupTranslation[expected.type].category;
    if (expected.category == nullptr) {
      CHECK_TRUE(actual == nullptr);
      continue;
    }
    STRCMP_EQUAL(expected.category, actual);
  }
}

TEST(erd_lists, table_is_indexed_by_appliance_type_without_gaps)
{
  // maximumApplianceType is the table length; the polling bridge and the
  // discovery filter both index it directly by the ERD 0x0008 value, so the
  // entry at index N must describe appliance type N. The generated comments
  // carry the upstream display name; the enum's largest value is 58.
  CHECK_EQUAL(59u, maximumApplianceType);
  CHECK_TRUE(applianceTypeToErdGroupTranslation[0].category != nullptr);
  CHECK_TRUE(applianceTypeToErdGroupTranslation[58].erdList == nullptr);
}
