/*!
 * @file
 * @brief ErdPollListBuilder implementation.
 */

#include "erd_poll_list_builder.h"
#include "erd_lists.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

GEA_TAG(TAG) = "erd_poll_list_builder";

namespace esphome {
namespace geappliances_bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/* Append standard ERDs (no board address — default to primary). */
static uint16_t append_standard_erds(probe_entry_t* out, uint16_t count,
                                     const tiny_erd_t* list, uint16_t list_count)
{
  uint16_t n = (list_count > (ERD_POLL_LIST_MAX_SIZE - count))
              ? (ERD_POLL_LIST_MAX_SIZE - count) : list_count;
  if (n < list_count) {
    ESP_LOGW(TAG, "Poll list capacity exceeded; truncated %u ERDs (dropped %u)",
             n, list_count - n);
  }
  if (list && n > 0) {
    for (uint16_t i = 0; i < n; i++) {
      out[count + i] = {list[i], PROBE_ENTRY_DEFAULT_ADDRESS};
    }
  }
  return count + n;
}

/* Append custom ERDs that carry per-ERD board addresses. */
static uint16_t append_custom_erds(probe_entry_t* out, uint16_t count,
                                   const probe_entry_t* list, uint16_t list_count)
{
  uint16_t n = (list_count > (ERD_POLL_LIST_MAX_SIZE - count))
              ? (ERD_POLL_LIST_MAX_SIZE - count) : list_count;
  if (n < list_count) {
    ESP_LOGW(TAG, "Poll list capacity exceeded; truncated %u custom ERDs (dropped %u)",
             n, list_count - n);
  }
  if (list && n > 0) {
    std::memcpy(out + count, list, n * sizeof(probe_entry_t));
  }
  return count + n;
}

/* Compare probe_entry_t for sorting: by (erd, board_address). */
static int probe_entry_cmp(const void* a, const void* b)
{
  const probe_entry_t* pa = (const probe_entry_t*)a;
  const probe_entry_t* pb = (const probe_entry_t*)b;
  if (pa->erd < pb->erd) return -1;
  if (pa->erd > pb->erd) return 1;
  if (pa->board_address < pb->board_address) return -1;
  if (pa->board_address > pb->board_address) return 1;
  return 0;
}

/* Deduplicate on (erd, board_address) pairs. */
static void deduplicate(probe_entry_t* erds, uint16_t& count)
{
  if (count <= 1) return;
  qsort(erds, count, sizeof(probe_entry_t), probe_entry_cmp);
  uint16_t write = 1;
  for (uint16_t read = 1; read < count; read++) {
    if (erds[read].erd != erds[write - 1].erd ||
        erds[read].board_address != erds[write - 1].board_address) {
      erds[write++] = erds[read];
    }
  }
  count = write;
}

ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config)
{
  ErdPollListResult result;
  std::memset(result.erds, 0, sizeof(result.erds));
  result.erds_count = 0;
  result.description = "";

  bool is_subscribe_mode = (config.mode == BRIDGE_MODE_SUBSCRIBE) ||
                           (config.mode == BRIDGE_MODE_AUTO && config.subscription_active);

  if (is_subscribe_mode) {
    result.erds_count = append_custom_erds(result.erds, 0,
                                           config.custom_erds, config.custom_erds_count);
    result.description = "subscription mode: custom ERDs only";
    return result;
  }

  if (config.appliance_api_parsing) {
    result.erds_count = append_standard_erds(result.erds, 0,
                                             config.feature_bit_valid_erds,
                                             config.feature_bit_valid_erds_count);
    result.erds_count = append_custom_erds(result.erds, result.erds_count,
                                           config.custom_erds, config.custom_erds_count);
    result.description = "poll mode with API parsing: feature-bit ERDs + custom ERDs";
  } else {
    result.erds_count = append_standard_erds(result.erds, 0, commonErds, commonErdCount);
    result.erds_count = append_standard_erds(result.erds, result.erds_count,
                                             energyErds, energyErdCount);
    result.erds_count = append_standard_erds(result.erds, result.erds_count,
                                             applianceApiFeatureErds, applianceApiFeatureErdCount);

    if (config.appliance_type < maximumApplianceType) {
      const auto& group = applianceTypeToErdGroupTranslation[config.appliance_type];
      result.erds_count = append_standard_erds(result.erds, result.erds_count,
                                               group.erdList, group.erdCount);
    }

    result.erds_count = append_custom_erds(result.erds, result.erds_count,
                                           config.custom_erds, config.custom_erds_count);
    result.description = "poll mode without API parsing: full ERD list";
  }

  deduplicate(result.erds, result.erds_count);

  return result;
}

}  // namespace geappliances_bridge
}  // namespace esphome
