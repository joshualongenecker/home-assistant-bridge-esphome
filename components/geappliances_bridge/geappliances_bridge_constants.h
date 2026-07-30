#pragma once

/*!
 * @file
 * @brief Shared ERD constants and inline helpers used across the bridge
 *        implementation files.
 *
 * All constants are defined as static constexpr so that each translation
 * unit gets its own copy without ODR violations.  Inline helper functions
 * follow the same rule.
 */

#include <cstdint>

#include "geappliances_bridge_log.h"

extern "C" {
#include "tiny_gea3_erd_client.h"
}

// ---------------------------------------------------------------------------
// Well-known GEA ERD identifiers
// ---------------------------------------------------------------------------

// Device identity ERDs (used for device ID generation)
static constexpr tiny_erd_t ERD_MODEL_NUMBER    = 0x0001;
static constexpr tiny_erd_t ERD_SERIAL_NUMBER   = 0x0002;
static constexpr tiny_erd_t ERD_APPLIANCE_TYPE  = 0x0008;

// Appliance API feature bit ERDs
// ERD 0x0092 reports common-feature flags; 0x0093–0x0097 and 0x0109–0x010D
// each report one appliance-specific API group.
static constexpr tiny_erd_t ERD_COMMON_FEATURE_API      = 0x0092;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_0 = 0x0093;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_1 = 0x0094;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_2 = 0x0095;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_3 = 0x0096;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_4 = 0x0097;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_5 = 0x0109;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_6 = 0x010A;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_7 = 0x010B;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_8 = 0x010C;
static constexpr tiny_erd_t ERD_APPLIANCE_FEATURE_API_9 = 0x010D;

// GEA bus broadcast address (all nodes respond)
static constexpr uint8_t GEA_BROADCAST_ADDRESS = 0xFF;

// Default boot stabilization delay before MQTT and appliance traffic starts.
static constexpr uint32_t AUTODISCOVERY_STARTUP_DELAY_MS = 5000;

// Each appliance feature API ERD has the layout [2B type][2B version][4B bitmap]
static constexpr uint8_t APPLIANCE_FEATURE_ERD_SIZE = 8;

// ---------------------------------------------------------------------------
// Inline helper functions
// ---------------------------------------------------------------------------

// Returns true when @p erd is one of the 11 appliance API feature bit ERDs.
static inline bool is_feature_bit_erd(tiny_erd_t erd)
{
  return erd == ERD_COMMON_FEATURE_API ||
         erd == ERD_APPLIANCE_FEATURE_API_0 ||
         erd == ERD_APPLIANCE_FEATURE_API_1 ||
         erd == ERD_APPLIANCE_FEATURE_API_2 ||
         erd == ERD_APPLIANCE_FEATURE_API_3 ||
         erd == ERD_APPLIANCE_FEATURE_API_4 ||
         erd == ERD_APPLIANCE_FEATURE_API_5 ||
         erd == ERD_APPLIANCE_FEATURE_API_6 ||
         erd == ERD_APPLIANCE_FEATURE_API_7 ||
         erd == ERD_APPLIANCE_FEATURE_API_8 ||
         erd == ERD_APPLIANCE_FEATURE_API_9;
}

// Reads up to 8 bytes from a big-endian byte buffer as a 64-bit integer.
// GEA protocol transmits ERD values MSB-first (big-endian).
static inline uint64_t read_be64(const uint8_t* buf, uint8_t size)
{
  if (!buf || size == 0) return 0;
  uint64_t bits = 0;
  uint8_t  n    = (size < 8u) ? size : 8u;
  for (uint8_t i = 0; i < n; i++) {
    bits = (bits << 8) | buf[i];
  }
  return bits;
}
