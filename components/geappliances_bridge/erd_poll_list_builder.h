#pragma once
#include <cstdint>

#include "tiny_erd.h"
#include "erd_lists.h"
#include "bridge_mode.h"
#include "erd_bridge_common.h"

namespace esphome {
namespace geappliances_bridge {

#define ERD_POLL_LIST_MAX_SIZE POLLING_LIST_MAX_SIZE

struct ErdPollListConfig {
  BridgeMode mode;

  bool subscription_active;

  bool appliance_api_parsing;

  const tiny_erd_t* feature_bit_valid_erds;
  uint16_t feature_bit_valid_erds_count;
  const probe_entry_t* custom_erds;
  uint16_t custom_erds_count;

  uint8_t appliance_type;
};

struct ErdPollListResult {
  probe_entry_t erds[ERD_POLL_LIST_MAX_SIZE];
  uint16_t erds_count;

  const char* description;
};

ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config);

}
}
