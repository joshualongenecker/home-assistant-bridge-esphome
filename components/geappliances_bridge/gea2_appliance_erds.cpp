/*!
 * @file
 * @brief ERD list access for GEA2 appliances
 * 
 * This file wraps the existing erd_lists.h to provide GEA2-specific access
 */

#include "gea2_appliance_erds.h"
#include "erd_lists.h"

// Wrap common ERDs
static const gea2_erd_list_t common_erd_list = { commonErds, commonErdCount };

const gea2_erd_list_t* gea2_get_common_erd_list(void) {
  return &common_erd_list;
}

// Wrap energy ERDs  
static const gea2_erd_list_t energy_erd_list = { energyErds, energyErdCount };

const gea2_erd_list_t* gea2_get_energy_erd_list(void) {
  return &energy_erd_list;
}

// Map appliance types to their ERD groups using existing lists
const gea2_erd_list_t* gea2_get_appliance_erd_list(uint8_t appliance_type) {
  // Maximum appliance type is 0x36 (54 decimal), so we need 55 entries (0-54)
  static constexpr size_t MAX_APPLIANCE_TYPE_COUNT = 55;
  static gea2_erd_list_t appliance_lists[MAX_APPLIANCE_TYPE_COUNT];
  
  // Initialize the mapping (only done once)
  static bool initialized = false;
  if (!initialized) {
    appliance_lists[0x00] = { waterHeaterErds, waterHeaterErdCount };
    appliance_lists[0x01] = { laundryErds, laundryErdCount };
    appliance_lists[0x02] = { laundryErds, laundryErdCount };
    appliance_lists[0x03] = { refrigerationErds, refrigerationErdCount };
    appliance_lists[0x04] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x05] = { rangeErds, rangeErdCount };
    appliance_lists[0x06] = { dishWasherErds, dishWasherErdCount };
    appliance_lists[0x07] = { rangeErds, rangeErdCount };
    appliance_lists[0x08] = { rangeErds, rangeErdCount };
    appliance_lists[0x09] = { rangeErds, rangeErdCount };
    appliance_lists[0x0A] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x0B] = { rangeErds, rangeErdCount };
    appliance_lists[0x0C] = { rangeErds, rangeErdCount };
    appliance_lists[0x0D] = { rangeErds, rangeErdCount };
    appliance_lists[0x0E] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x0F] = { rangeErds, rangeErdCount };
    appliance_lists[0x10] = { waterFilterErds, waterFilterErdCount };
    appliance_lists[0x11] = { rangeErds, rangeErdCount };
    appliance_lists[0x12] = { refrigerationErds, refrigerationErdCount };
    appliance_lists[0x13] = { rangeErds, rangeErdCount };
    appliance_lists[0x14] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x15] = { waterFilterErds, waterFilterErdCount };
    appliance_lists[0x16] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x17] = { laundryErds, laundryErdCount };
    appliance_lists[0x18] = { refrigerationErds, refrigerationErdCount };
    appliance_lists[0x19] = { refrigerationErds, refrigerationErdCount };
    appliance_lists[0x1A] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x1B] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x1C] = { refrigerationErds, refrigerationErdCount };
    appliance_lists[0x1D] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x1E] = { refrigerationErds, refrigerationErdCount };
    appliance_lists[0x1F] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x20] = { dishWasherErds, dishWasherErdCount };
    appliance_lists[0x21] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x22] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x23] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x24] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x25] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x26] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x27] = { rangeErds, rangeErdCount };
    appliance_lists[0x28] = { rangeErds, rangeErdCount };
    appliance_lists[0x29] = { rangeErds, rangeErdCount };
    appliance_lists[0x2A] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x2B] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x2C] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x2D] = { laundryErds, laundryErdCount };
    appliance_lists[0x2E] = { laundryErds, laundryErdCount };
    appliance_lists[0x2F] = { rangeErds, rangeErdCount };
    appliance_lists[0x30] = { refrigerationErds, refrigerationErdCount };
    appliance_lists[0x31] = { rangeErds, rangeErdCount };
    appliance_lists[0x32] = { smallApplianceErds, smallApplianceErdCount };
    appliance_lists[0x33] = { refrigerationErds, refrigerationErdCount };
    appliance_lists[0x34] = { airConditioningErds, airConditioningErdCount };
    appliance_lists[0x35] = { rangeErds, rangeErdCount };
    appliance_lists[0x36] = { smallApplianceErds, smallApplianceErdCount };
    initialized = true;
  }
  
  if (appliance_type >= MAX_APPLIANCE_TYPE_COUNT) {
    appliance_type = 0;  // Default to water heater
  }
  
  return &appliance_lists[appliance_type];
}
