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

#define HA_DISCOVERY_MAX_ERDS POLLING_LIST_MAX_SIZE

#define HA_DISCOVERY_DECOMP_BUF_SIZE 18432

#define HA_DISCOVERY_LINE_BUF_SIZE 18432

#define HA_DISCOVERY_TOPIC_BUF_SIZE 192
#define HA_DISCOVERY_FIELD_ID_BUF_SIZE 72
#define HA_DISCOVERY_UNIQUE_ID_BUF_SIZE 160
#define HA_DISCOVERY_PAYLOAD_BUF_SIZE 8192
#define HA_DISCOVERY_AVAILABILITY_TOPIC_BUF_SIZE 128
#define HA_DISCOVERY_AVAILABILITY_PAYLOAD_BUF_SIZE 32

typedef struct {
  uint16_t erd;
  uint8_t board_address;
} ha_discovery_board_erd_t;

typedef struct {
  erd_cache_t* cache;
  i_mqtt_client_t* mqtt_client;
  const char* device_id;
  const char* model_number;
  const char* serial_number;
  uint8_t appliance_type;

  bool filter_config_topics;
  ha_discovery_state_t state;

  uint32_t total_discovered;
  uint32_t total_published;
  uint32_t total_filtered;
  uint32_t total_uncached_configured;

  bool line_uncached_configured;

  uint16_t sorted_erds[HA_DISCOVERY_MAX_ERDS];
  uint16_t sorted_erds_count;

  tinfl_decompressor* decomp_state;
  uint8_t* decomp_buf;

  char* line_buf;

  char topic_buf[HA_DISCOVERY_TOPIC_BUF_SIZE];
  char* payload_buf;

  char device_json_buf[512];

  char availability_topic_buf[HA_DISCOVERY_AVAILABILITY_TOPIC_BUF_SIZE];
  char payload_available_buf[HA_DISCOVERY_AVAILABILITY_PAYLOAD_BUF_SIZE];
  char payload_not_available_buf[HA_DISCOVERY_AVAILABILITY_PAYLOAD_BUF_SIZE];

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
  const ha_discovery_board_erd_t* configured_board_erds;
  uint16_t configured_board_erds_count;
  ha_discovery_cleanup_t cleanup;

  /* Time injection — mirrors erd_bridge_poll_t::get_time_ms and
   * erd_cache_mqtt_publisher_t::get_time_ms. ha_discovery_manager_init()
   * installs esphome::millis; tests install a fake through
   * ha_discovery_manager_set_time_fn() so the publish-backoff window is
   * exercised against a deterministic clock instead of the HAL double's frozen
   * counter. run() does no publish work while this is NULL, which only ever
   * happens after ha_discovery_manager_cleanup() zeroed the struct. */
  uint32_t (*get_time_ms)(void);

  uint32_t retry_backoff_until_ms;

  char domain_topic_prefix[128];
  char current_domain_prefix_buf[32];

} ha_discovery_manager_t;

void ha_discovery_manager_init(ha_discovery_manager_t* self);
void ha_discovery_manager_set_custom_data(ha_discovery_manager_t* self,
  const uint8_t* data, const void* chunks, uint16_t num_chunks, uint16_t max_chunk, uint32_t data_hash);

void ha_discovery_manager_set_configured_board_erds(ha_discovery_manager_t* self,
  const ha_discovery_board_erd_t* erds, uint16_t count);

void ha_discovery_manager_configure(
  ha_discovery_manager_t* self,
  const char* device_id,
  const char* model_number,
  const char* serial_number,
  uint8_t appliance_type,
  bool filter_config_topics,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client);

void ha_discovery_manager_set_availability(
  ha_discovery_manager_t* self,
  const char* topic,
  const char* payload_available,
  const char* payload_not_available);

uint32_t ha_discovery_manager_data_hash(const ha_discovery_manager_t* self);

void ha_discovery_manager_start(ha_discovery_manager_t* self);

void ha_discovery_manager_run(ha_discovery_manager_t* self);

uint32_t ha_discovery_manager_run_allocation_size(void);

/* Override the time source after init (test seam; mirrors
 * erd_bridge_poll_set_time_fn). MUST be called after
 * ha_discovery_manager_init(): init installs the HAL default unconditionally
 * (the struct is a plain member that may hold garbage, so a preserve-if-
 * non-NULL check would read garbage and skip the default). A NULL argument
 * restores the default. */
void ha_discovery_manager_set_time_fn(
  ha_discovery_manager_t* self,
  uint32_t (*get_time_ms)(void));

void ha_discovery_manager_cleanup(ha_discovery_manager_t* self);

bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self);

ha_discovery_state_t ha_discovery_manager_get_state(ha_discovery_manager_t* self);

uint32_t ha_discovery_manager_total_published(const ha_discovery_manager_t* self);

#ifdef HA_DISCOVERY_TEST_EXPORT
void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg);
void cleanup_start(ha_discovery_cleanup_t* self);
uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self);
bool ha_discovery_test_build_payload(ha_discovery_manager_t* self, const char* line);
#endif

#ifdef __cplusplus
}
#endif

#endif
