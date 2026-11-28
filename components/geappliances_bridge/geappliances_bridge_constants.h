#pragma once

#include <cstdint>

#include "geappliances_bridge_log.h"

extern "C" {
#include "tiny_gea3_erd_client.h"
}

static constexpr tiny_erd_t ERD_MODEL_NUMBER    = 0x0001;
static constexpr tiny_erd_t ERD_SERIAL_NUMBER   = 0x0002;
static constexpr tiny_erd_t ERD_APPLIANCE_TYPE  = 0x0008;

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

static constexpr uint8_t GEA_BROADCAST_ADDRESS = 0xFF;

static constexpr uint32_t AUTODISCOVERY_STARTUP_DELAY_MS = 10000;

static constexpr uint8_t APPLIANCE_FEATURE_ERD_SIZE = 8;

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
