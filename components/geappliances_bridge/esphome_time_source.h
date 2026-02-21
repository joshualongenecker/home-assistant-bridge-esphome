#pragma once

extern "C" {
#include "i_tiny_time_source.h"
}

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Initialize the ESPHome time source.
 * @return The time source interface.
 */
i_tiny_time_source_t* esphome_time_source_init(void);

#ifdef __cplusplus
}
#endif
