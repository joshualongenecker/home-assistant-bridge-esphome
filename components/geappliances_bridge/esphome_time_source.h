// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Provide ESPHome's millis()-based monotonic clock as an
//       i_tiny_time_source_t for use by the tiny library.
//
// Responsibilities:
//   - Implement i_tiny_time_source_t backed by millis()
//
// NOT responsible for:
//   - Any logic beyond returning the current time
//
// Dependencies:
//   - i_tiny_time_source.h
// =============================================================================

#pragma once

extern "C" {
#include "i_tiny_time_source.h"
}

#ifdef __cplusplus
extern "C" {
#endif

i_tiny_time_source_t* esphome_time_source_init(void);

#ifdef __cplusplus
}
#endif
