/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager.
 *
 * Main-loop design: start() builds the sorted ERD list and device JSON
 * inline. run() is called from the main loop; it decompresses chunks,
 * parses JSONL, and publishes one entity per call, keeping loop times low.
 *
 * States: IDLE -> BUILDING -> DISCOVERING -> COMPLETE / FAILED
 *
 * Cleanup is handled by the embedded ha_discovery_cleanup_t module.
 *
 * All buffers are pre-allocated — no heap allocation during processing.
 * Peak memory: payload buffer (~8 KB) + decompress buffer (18 KB) +
 * line buffer (18 KB) + sorted ERD array (~1.3 KB).
 */

#ifndef ha_discovery_manager_h
#define ha_discovery_manager_h

#include <stdint.h>
#include <stdbool.h>

#include "erd_cache.h"
#include "erd_lists.h"
#include "i_mqtt_client.h"
#include "ha_discovery_cleanup.h"

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

#ifdef USE_ESP_IDF_STUBS
  #include "miniz_tinfl.h"
#else
  #include "miniz.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Discovery manager states */
typedef enum {
  ha_discovery_state_idle,
  ha_discovery_state_building,     // building sorted ERD list
  ha_discovery_state_discovering,  // main loop decompressing/publishing
  ha_discovery_state_complete,
  ha_discovery_state_failed
} ha_discovery_state_t;

/* Maximum number of registered/seen ERDs for HA discovery binary search.
   Uses POLLING_LIST_MAX_SIZE from erd_lists.h as the single source of truth. */
#define HA_DISCOVERY_MAX_ERDS POLLING_LIST_MAX_SIZE


/* Decompression buffer size per chunk. Must be >= max decompressed chunk
   size (range category has 17770 bytes). */
#define HA_DISCOVERY_DECOMP_BUF_SIZE 18432

/* Line parsing buffer size (matches decomp buffer). */
#define HA_DISCOVERY_LINE_BUF_SIZE 18432

/* Topic buffer size for HA discovery topics (must fit worst-case topic + null). */
#define HA_DISCOVERY_TOPIC_BUF_SIZE 192
/* Field ID slug buffer size. */
#define HA_DISCOVERY_FIELD_ID_BUF_SIZE 72
/* Unique ID buffer size. */
#define HA_DISCOVERY_UNIQUE_ID_BUF_SIZE 160
/* Payload buffer for building discovery payloads. */
#define HA_DISCOVERY_PAYLOAD_BUF_SIZE 8192

/*!
 * @brief Home Assistant MQTT Discovery manager.
 *
 * Peak memory: payload buffer (~8 KB) + decompress buffer (~18 KB) +
 * line buffer (~18 KB) + sorted ERD array (~1.3 KB).
 */
typedef struct {
  erd_cache_t* cache;              // Shared ERD cache (owned by GeappliancesBridge)
  i_mqtt_client_t* mqtt_client;    // MQTT publish interface
  const char* device_id;           // Device ID string for topic construction
  const char* model_number;        // Model number for device info
  const char* serial_number;       // Serial number for device info
  uint8_t appliance_type;          // Appliance type for category filtering

  bool filter_config_topics;       /* Whether config topic filtering was enabled */
  ha_discovery_state_t state;

  /* Stats */
  uint32_t total_discovered;       // Total entities discovered
  uint32_t total_published;        // Total discovery publishes
  uint32_t total_filtered;         // Entities filtered out (ERD not registered)


  /* Sorted ERD array for binary search during discovery. */
  uint16_t sorted_erds[HA_DISCOVERY_MAX_ERDS];
  uint16_t sorted_erds_count;

  tinfl_decompressor decomp_state;
  /* Decompression buffer for JSONL chunks (18KB). */
  uint8_t decomp_buf[HA_DISCOVERY_DECOMP_BUF_SIZE];

  /* Line parsing buffer. */
  char line_buf[HA_DISCOVERY_LINE_BUF_SIZE];

  /* Payload buffer for building discovery payloads. */
  char topic_buf[HA_DISCOVERY_TOPIC_BUF_SIZE];
  char payload_buf[HA_DISCOVERY_PAYLOAD_BUF_SIZE];


  /* Device JSON built once at start. */
  char device_json_buf[512];

  /* Entity field buffers (used by process_jsonl_line to avoid stack overflow).
   * Templates are NOT stored here — they are embedded directly from the raw
   * JSONL line into the payload buffer with proper re-escaping. */
  char entity_name_buf[160];
  char erd_id_hex_buf[8];
  char domain_buf[32];
  char field_id_buf[HA_DISCOVERY_FIELD_ID_BUF_SIZE];
  char board_address_buf[4];       /* optional two-digit JSONL board address */
  char paired_erd_buf[8];
  char role_buf[16];
  char unit_buf[32];
  char device_class_buf[32];
  char state_class_buf[32];
  char options_buf[256];
  char data_type_buf[16];
  char scale_factor_buf[16];
  char min_buf[32];
  char max_buf[32];
  char step_buf[32];
  char mode_buf[16];
  char payload_on_buf[16];
  char payload_off_buf[16];
  char state_on_buf[16];
  char state_off_buf[16];
  char unique_id_buf[HA_DISCOVERY_UNIQUE_ID_BUF_SIZE];
  char state_topic_buf[128];
  char command_topic_buf[128];
  char actual_state_topic_buf[128];
  char actual_command_topic_buf[128];

  /* Discovery progress tracking. */
  uint16_t current_category;       // Index into ha_discovery_categories[]
  uint16_t current_chunk;          // Index into current category's chunks
  uint32_t current_offset;         // Byte offset within decompressed chunk
  uint32_t current_decomp_size;    // Size of current decompressed chunk
  const uint8_t* custom_data;      // Optional codegen-provided compressed discovery data
  const void* custom_chunks;
  uint16_t custom_num_chunks;
  uint16_t custom_max_decompressed_chunk;
  uint32_t custom_data_hash;
  /* Embedded cleanup module for removing old discovery topics. */
  ha_discovery_cleanup_t cleanup;

  /* Domain topic prefix: pre-computed "homeassistant/{domain}/{device_id}/"
   * to avoid repeated snprintf during discovery publish. */
  char domain_topic_prefix[128];
  char current_domain_prefix_buf[32]; // Tracks current domain for prefix caching

} ha_discovery_manager_t;

/*!
 * Initialize the discovery manager. Call once before configure().
 */
void ha_discovery_manager_init(ha_discovery_manager_t* self);
void ha_discovery_manager_set_custom_data(ha_discovery_manager_t* self,
  const uint8_t* data, const void* chunks, uint16_t num_chunks, uint16_t max_chunk, uint32_t data_hash);

/*!
 * Configure the discovery manager with device info and dependencies.
 * Call after init(), before start().
 */
void ha_discovery_manager_configure(
  ha_discovery_manager_t* self,
  const char* device_id,
  const char* model_number,
  const char* serial_number,
  uint8_t appliance_type,
  bool filter_config_topics,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client);

/*!
 * Start the discovery process.
 * Builds the sorted ERD list and device JSON inline, then
 * transitions to DISCOVERING state.
 */
void ha_discovery_manager_start(ha_discovery_manager_t* self);

/*!
 * Drive the discovery: decompress chunks and publish entities.
 * Call from the main loop while the manager is in BUILDING or DISCOVERING state.
 * Publishes one entity per call, keeping loop times low.
 * Transitions to COMPLETE when all entities are published.
 */
void ha_discovery_manager_run(ha_discovery_manager_t* self);

/*!
 * Clean up the discovery manager.
 * Stops tasks and frees resources. Call from teardown.
 */
void ha_discovery_manager_cleanup(ha_discovery_manager_t* self);


/*!
 * Returns true if the manager is currently processing (building or discovering).
 */
bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self);

/*!
 * Returns the current state.
 */
ha_discovery_state_t ha_discovery_manager_get_state(ha_discovery_manager_t* self);

/* Test-only exports: exposed when HA_DISCOVERY_TEST_EXPORT is defined. */
#ifdef HA_DISCOVERY_TEST_EXPORT
void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg);
void cleanup_start(ha_discovery_cleanup_t* self);
uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self);
void ha_discovery_test_format_erd_topic(char* destination, size_t destination_size,
                                        const char* device_id, const char* erd_id,
                                        const char* board_address, const char* operation);
#endif

#ifdef __cplusplus
}
#endif

#endif
