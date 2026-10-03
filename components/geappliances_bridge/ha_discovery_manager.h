/*!
 * Main-loop design: start() builds the sorted ERD list and device JSON
 * inline; run() is called from the main loop and publishes one entity per
 * call, keeping loop times low.
 *
 * States: IDLE -> BUILDING -> DISCOVERING -> COMPLETE / FAILED
 *
 * The three large buffers (decomp ~18 KB, line ~18 KB, payload ~8 KB) are
 * heap-allocated only for the duration of a discovery run; on non-discovery
 * boots they are never allocated. Peak memory during discovery: those three
 * buffers + the sorted ERD array (~1.3 KB).
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

typedef enum {
  ha_discovery_state_idle,
  ha_discovery_state_building,
  ha_discovery_state_discovering,
  ha_discovery_state_complete,
  ha_discovery_state_failed
} ha_discovery_state_t;

/* Uses POLLING_LIST_MAX_SIZE from erd_lists.h as the single source of truth. */
#define HA_DISCOVERY_MAX_ERDS POLLING_LIST_MAX_SIZE

/* Must be >= max decompressed chunk size (range category has 17770 bytes). */
#define HA_DISCOVERY_DECOMP_BUF_SIZE 18432

/* Matches the decomp buffer size. */
#define HA_DISCOVERY_LINE_BUF_SIZE 18432

/* Topic buffer size for HA discovery topics (must fit worst-case topic + null). */
#define HA_DISCOVERY_TOPIC_BUF_SIZE 192
#define HA_DISCOVERY_FIELD_ID_BUF_SIZE 72
#define HA_DISCOVERY_UNIQUE_ID_BUF_SIZE 160
#define HA_DISCOVERY_PAYLOAD_BUF_SIZE 8192
#define HA_DISCOVERY_AVAILABILITY_TOPIC_BUF_SIZE 128
#define HA_DISCOVERY_AVAILABILITY_PAYLOAD_BUF_SIZE 32

/*!
 * Peak memory: payload buffer (~8 KB) + decompress buffer (~18 KB) +
 * line buffer (~18 KB) + sorted ERD array (~1.3 KB).
 */
typedef struct {
  erd_cache_t* cache;              // Shared ERD cache (owned by GeappliancesBridge)
  i_mqtt_client_t* mqtt_client;
  const char* device_id;
  const char* model_number;
  const char* serial_number;
  uint8_t appliance_type;

  bool filter_config_topics;
  ha_discovery_state_t state;

  uint32_t total_discovered;
  uint32_t total_published;
  uint32_t total_filtered;         // Entities filtered out (ERD not registered)

  uint16_t sorted_erds[HA_DISCOVERY_MAX_ERDS];
  uint16_t sorted_erds_count;

  tinfl_decompressor decomp_state;
  /* Heap-allocated only while a discovery run is active; NULL otherwise, so
   * non-discovery boots don't carry this ~44 KB in the object. */
  uint8_t* decomp_buf;

  /* Line parsing buffer. Heap-allocated only while a discovery run is active. */
  char* line_buf;

  /* Topic buffer (small; stays a static member). */
  char topic_buf[HA_DISCOVERY_TOPIC_BUF_SIZE];
  /* Heap-allocated only while a discovery run is active. */
  char* payload_buf;

  char device_json_buf[512];

  /* ESPHome birth/LWT availability, copied by set_availability(). An empty
   * topic omits availability from every discovery payload. */
  char availability_topic_buf[HA_DISCOVERY_AVAILABILITY_TOPIC_BUF_SIZE];
  char payload_available_buf[HA_DISCOVERY_AVAILABILITY_PAYLOAD_BUF_SIZE];
  char payload_not_available_buf[HA_DISCOVERY_AVAILABILITY_PAYLOAD_BUF_SIZE];

  /* Entity field buffers (avoid stack overflow in process_jsonl_line).
   * Templates are NOT stored here — they are embedded directly from the raw
   * JSONL line into the payload buffer with proper re-escaping. */
  char entity_name_buf[160];
  char erd_id_hex_buf[8];
  char domain_buf[32];
  char field_id_buf[HA_DISCOVERY_FIELD_ID_BUF_SIZE];
  char board_address_buf[4];
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

  uint16_t current_category;
  uint16_t current_chunk;
  uint32_t current_offset;
  uint32_t current_decomp_size;
  const uint8_t* custom_data;
  const void* custom_chunks;
  uint16_t custom_num_chunks;
  uint16_t custom_max_decompressed_chunk;
  uint32_t custom_data_hash;
  ha_discovery_cleanup_t cleanup;

  /* Pre-computed "homeassistant/{domain}/{device_id}/" prefix to avoid
   * repeated snprintf during discovery publish. */
  char domain_topic_prefix[128];
  char current_domain_prefix_buf[32];

} ha_discovery_manager_t;

/*!
 * Call once before configure().
 */
void ha_discovery_manager_init(ha_discovery_manager_t* self);
void ha_discovery_manager_set_custom_data(ha_discovery_manager_t* self,
  const uint8_t* data, const void* chunks, uint16_t num_chunks, uint16_t max_chunk, uint32_t data_hash);

/*!
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
 * Call after configure(), before start(). Adds an availability topic to
 * every discovery payload so HA marks entities unavailable when the bridge's
 * MQTT last will fires. payload_available / payload_not_available are only
 * emitted when they differ from HA's defaults ("online" / "offline"); NULL
 * means the default. A NULL/empty topic, or a value that is too long or not
 * JSON-safe, leaves availability disabled.
 */
void ha_discovery_manager_set_availability(
  ha_discovery_manager_t* self,
  const char* topic,
  const char* payload_available,
  const char* payload_not_available);

/*!
 * Fingerprint of everything that determines the published discovery payloads:
 * the generated data (HA_DISCOVERY_DATA_HASH), the optional custom profile,
 * and the runtime-injected availability topic/payloads. Used by
 * OtaCleanupManager for change detection: any change to these inputs must
 * change this value so a cleanup/republish is triggered. With availability
 * unset the value is identical to pre-availability firmware, so those
 * installs do not get a spurious republish.
 */
uint32_t ha_discovery_manager_data_hash(const ha_discovery_manager_t* self);

void ha_discovery_manager_start(ha_discovery_manager_t* self);

void ha_discovery_manager_run(ha_discovery_manager_t* self);

void ha_discovery_manager_cleanup(ha_discovery_manager_t* self);

bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self);

ha_discovery_state_t ha_discovery_manager_get_state(ha_discovery_manager_t* self);

/* Test-only exports. */
#ifdef HA_DISCOVERY_TEST_EXPORT
void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg);
void cleanup_start(ha_discovery_cleanup_t* self);
uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self);
void ha_discovery_test_format_erd_topic(char* destination, size_t destination_size,
                                        const char* device_id, const char* erd_id,
                                        const char* board_address, const char* operation);
bool ha_discovery_test_build_payload(ha_discovery_manager_t* self, const char* line);
#endif

#ifdef __cplusplus
}
#endif

#endif
