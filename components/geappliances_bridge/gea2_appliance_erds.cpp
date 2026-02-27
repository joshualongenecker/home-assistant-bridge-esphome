/*!
 * @file
 * @brief ERD list access for GEA2 appliances
 * 
 * This file wraps the existing erd_lists.h to provide GEA2-specific access
 */

#include "gea2_appliance_erds.h"
#include "erd_lists.h"
#include <cstddef>

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
  
  // Initialize the mapping with all appliance types
  static const gea2_erd_list_t appliance_lists[MAX_APPLIANCE_TYPE_COUNT] = {
    { waterHeaterErds, waterHeaterErdCount },         // 0x00
    { laundryErds, laundryErdCount },                 // 0x01
    { laundryErds, laundryErdCount },                 // 0x02
    { refrigerationErds, refrigerationErdCount },     // 0x03
    { smallApplianceErds, smallApplianceErdCount },   // 0x04
    { rangeErds, rangeErdCount },                     // 0x05
    { dishWasherErds, dishWasherErdCount },           // 0x06
    { rangeErds, rangeErdCount },                     // 0x07
    { rangeErds, rangeErdCount },                     // 0x08
    { rangeErds, rangeErdCount },                     // 0x09
    { airConditioningErds, airConditioningErdCount }, // 0x0A
    { rangeErds, rangeErdCount },                     // 0x0B
    { rangeErds, rangeErdCount },                     // 0x0C
    { rangeErds, rangeErdCount },                     // 0x0D
    { airConditioningErds, airConditioningErdCount }, // 0x0E
    { rangeErds, rangeErdCount },                     // 0x0F
    { waterFilterErds, waterFilterErdCount },         // 0x10
    { rangeErds, rangeErdCount },                     // 0x11
    { refrigerationErds, refrigerationErdCount },     // 0x12
    { rangeErds, rangeErdCount },                     // 0x13
    { airConditioningErds, airConditioningErdCount }, // 0x14
    { waterFilterErds, waterFilterErdCount },         // 0x15
    { airConditioningErds, airConditioningErdCount }, // 0x16
    { laundryErds, laundryErdCount },                 // 0x17
    { refrigerationErds, refrigerationErdCount },     // 0x18
    { refrigerationErds, refrigerationErdCount },     // 0x19
    { smallApplianceErds, smallApplianceErdCount },   // 0x1A
    { smallApplianceErds, smallApplianceErdCount },   // 0x1B
    { refrigerationErds, refrigerationErdCount },     // 0x1C
    { airConditioningErds, airConditioningErdCount }, // 0x1D
    { refrigerationErds, refrigerationErdCount },     // 0x1E
    { airConditioningErds, airConditioningErdCount }, // 0x1F
    { dishWasherErds, dishWasherErdCount },           // 0x20
    { smallApplianceErds, smallApplianceErdCount },   // 0x21
    { smallApplianceErds, smallApplianceErdCount },   // 0x22
    { airConditioningErds, airConditioningErdCount }, // 0x23
    { airConditioningErds, airConditioningErdCount }, // 0x24
    { smallApplianceErds, smallApplianceErdCount },   // 0x25
    { smallApplianceErds, smallApplianceErdCount },   // 0x26
    { rangeErds, rangeErdCount },                     // 0x27
    { rangeErds, rangeErdCount },                     // 0x28
    { rangeErds, rangeErdCount },                     // 0x29
    { smallApplianceErds, smallApplianceErdCount },   // 0x2A
    { smallApplianceErds, smallApplianceErdCount },   // 0x2B
    { airConditioningErds, airConditioningErdCount }, // 0x2C
    { laundryErds, laundryErdCount },                 // 0x2D
    { laundryErds, laundryErdCount },                 // 0x2E
    { rangeErds, rangeErdCount },                     // 0x2F
    { refrigerationErds, refrigerationErdCount },     // 0x30
    { rangeErds, rangeErdCount },                     // 0x31
    { smallApplianceErds, smallApplianceErdCount },   // 0x32
    { refrigerationErds, refrigerationErdCount },     // 0x33
    { airConditioningErds, airConditioningErdCount }, // 0x34
    { rangeErds, rangeErdCount },                     // 0x35
    { smallApplianceErds, smallApplianceErdCount },   // 0x36
  };
  
  if (appliance_type >= MAX_APPLIANCE_TYPE_COUNT) {
    appliance_type = 0;  // Default to water heater
  }
  
  return &appliance_lists[appliance_type];
}
