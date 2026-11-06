/*!
 * @file
 * @brief Tests for the generated appliance_type_map.h.
 *
 * The returned identifier is the first field of the generated device ID
 * (applianceType_model_serial), which is also the MQTT topic prefix and the
 * Home Assistant device identifier. A regeneration that renames a type would
 * silently orphan every entity on an already-installed bridge, so the whole
 * table is pinned here: changing one of these strings is a breaking change
 * that has to be a deliberate edit to this test.
 */

#include "appliance_type_map.h"

#include "CppUTest/TestHarness.h"

#include <cstdint>
#include <cstring>

namespace {

struct type_name {
  uint8_t type;
  const char* name;
};

const type_name kTypeNames[] = {
  {0, "WaterHeater"},
  {1, "ClothesDryer"},
  {2, "ClothesWasher"},
  {3, "Refrigerator"},
  {4, "Microwave"},
  {5, "Advantium"},
  {6, "Dishwasher"},
  {7, "Oven"},
  {8, "ElectricRange"},
  {9, "GasRange"},
  {10, "ThermostatRAC"},
  {11, "ElectricCooktop"},
  {12, "PizzaOven"},
  {13, "GasCooktop"},
  {14, "SplitDFSDuctFreeSplitAC"},
  {15, "Hood"},
  {16, "PointOfEntryWaterFilter"},
  {17, "InductionCooktop"},
  {18, "DeliveryBox"},
  {19, "KitchenHubVentHood"},
  {20, "ZonelinePTAC"},
  {21, "WaterSoftener"},
  {22, "PortableAC"},
  {23, "CombinationWasherDryer"},
  {24, "DualZoneWineChiller"},
  {25, "BeverageCenter"},
  {26, "CoffeeBrewer"},
  {27, "OpalNuggetIceMaker"},
  {28, "InHomeGrower"},
  {29, "Dehumidifier"},
  {30, "UnderCounterIceMaker"},
  {31, "ThroughWallAC"},
  {32, "FPDishDrawer"},
  {33, "EspressoCoffeeMaker"},
  {34, "ToasterOven"},
  {35, "ZonelineVertical"},
  {36, "CentralDFSDuctFreeSplitController"},
  {37, "BLEMeshGateway"},
  {38, "StandMixer"},
  {39, "FisherPaykelCooktop"},
  {40, "FPCooktopTeppanyaki"},
  {41, "FPVentilationDowndraft"},
  {42, "SmartPlug"},
  {43, "Smoker"},
  {44, "AirHandlerVRF"},
  {45, "FabricCareCabinetCloset"},
  {46, "LaundryCenter"},
  {47, "Grill"},
  {48, "Freezer"},
  {49, "WarmingDrawer"},
  {50, "VacuumSealDrawer"},
  {51, "WineCabinet"},
  {52, "CentralAC"},
  {53, "SoftStarter"},
  {54, "HearthPizzaOven"},
  {55, "SourdoughStarter"},
  {56, "Thermostat"},
  {57, "AirPurifier"},
  {58, "HomeVentilation"},
  {255, "Unknown"},
};

}  // namespace

TEST_GROUP(appliance_type_map)
{
  void setup() {}
};

TEST(appliance_type_map, identifiers_are_stable_for_every_known_type)
{
  for (const auto& entry : kTypeNames) {
    STRCMP_EQUAL(entry.name, appliance_type_to_string(entry.type));
  }
}

TEST(appliance_type_map, unknown_types_fall_back_to_unknown)
{
  // 59-254 are unassigned today; 0xFE is the last non-broadcast address value
  // and 0xFF is the GEA broadcast address, never a real appliance type.
  STRCMP_EQUAL("Unknown", appliance_type_to_string(59));
  STRCMP_EQUAL("Unknown", appliance_type_to_string(128));
  STRCMP_EQUAL("Unknown", appliance_type_to_string(254));
}

TEST(appliance_type_map, identifiers_are_safe_for_mqtt_topic_prefixes)
{
  // sanitize_for_mqtt_topic_() keeps alphanumerics and underscores; the
  // generated device ID is "<type>_<model>_<serial>", so an identifier with a
  // separator would shift the topic structure.
  for (const auto& entry : kTypeNames) {
    for (const char* p = appliance_type_to_string(entry.type); *p; p++) {
      CHECK_TRUE((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                 (*p >= '0' && *p <= '9') || *p == '_');
    }
  }
}
