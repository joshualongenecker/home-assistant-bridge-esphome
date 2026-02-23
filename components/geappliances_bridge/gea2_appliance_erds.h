/*!
 * @file
 * @brief ERD list access for various appliances (GEA2)
 */

#pragma once

extern "C" {
#include "tiny_erd.h"
}

typedef struct {
  const tiny_erd_t* erd_list;
  const uint16_t erd_count;
} gea2_erd_list_t;

/*!
 * Get the list of common ERDs
 */
const gea2_erd_list_t* gea2_get_common_erd_list(void);

/*!
 * Get the list of energy ERDs
 */
const gea2_erd_list_t* gea2_get_energy_erd_list(void);

/*!
 * Get the list of appliance ERDs based on appliance type
 */
const gea2_erd_list_t* gea2_get_appliance_erd_list(uint8_t appliance_type);
