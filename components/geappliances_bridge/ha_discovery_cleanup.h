#ifndef ha_discovery_cleanup_h
#define ha_discovery_cleanup_h

#include <stdint.h>
#include <stdbool.h>

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

#ifdef USE_ESP_IDF_STUBS
#include "esp-idf/freertos_stub.h"
#else
#include "freertos/FreeRTOS.h"
#endif

#include "i_mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ha_cleanup_state_idle,
  ha_cleanup_state_cleaning,
  ha_cleanup_state_done,
} ha_cleanup_state_t;

#define HA_CLEANUP_IDLE_TIMEOUT_MS 1000
#define HA_CLEANUP_MIN_SUBSCRIBE_MS 1000
#define HA_CLEANUP_DRAIN_WAIT_MS 1000
#define HA_CLEANUP_FLUSH_BATCH 1

#define HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS 64

#ifdef HA_CLEANUP_TEST_BUF_SIZE
  #define HA_CLEANUP_TOPIC_BUF_SIZE HA_CLEANUP_TEST_BUF_SIZE
#else
  #define HA_CLEANUP_TOPIC_BUF_SIZE 6144
#endif

typedef struct {
  i_mqtt_client_t* mqtt_client;
  const char* device_id;
  ha_cleanup_state_t state;
  uint32_t (*get_time_ms)(void);

  char* topic_buf;
  /* Allocation seam, installed by init() with the default malloc/free pair and
   * replaced by tests to drive the allocation-failure refusal and to watch the
   * queue's lifetime. */
  void *(*alloc_fn)(size_t size);
  void (*free_fn)(void *buffer);
  uint16_t queue_write_pos;
  uint16_t queue_count;
  uint16_t dropped_count;
  uint32_t total_removed_count;

  uint32_t last_activity_ms;
  uint32_t subscribe_start_ms;
  bool subscribed;
  bool flushed_once;
  uint8_t clean_passes;
  bool pass_found_topics;
  uint16_t pass_received_count;
  uint16_t pass_removed_count;
  uint8_t pass_number;

  uint32_t drain_start_ms;
  uint32_t retry_backoff_until_ms;

  portMUX_TYPE mux;
} ha_discovery_cleanup_t;

#ifdef __cplusplus
  static_assert(sizeof(ha_discovery_cleanup_t) <= 128,
                "ha_discovery_cleanup_t must not embed per-run buffers");
#else
  _Static_assert(sizeof(ha_discovery_cleanup_t) <= 128,
                 "ha_discovery_cleanup_t must not embed per-run buffers");
#endif

void ha_discovery_cleanup_init(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_configure(ha_discovery_cleanup_t* self,
    const char* device_id, i_mqtt_client_t* mqtt_client, uint32_t (*get_time_ms)(void));

bool ha_discovery_cleanup_start(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_run(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_destroy(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_release_queue(ha_discovery_cleanup_t* self);

ha_cleanup_state_t ha_discovery_cleanup_get_state(ha_discovery_cleanup_t* self);

bool ha_discovery_cleanup_is_done(ha_discovery_cleanup_t* self);

uint32_t ha_discovery_cleanup_total_removed(const ha_discovery_cleanup_t* self);

#ifdef HA_DISCOVERY_CLEANUP_TEST_EXPORT
void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg);
uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self);
void cleanup_start(ha_discovery_cleanup_t* self);
#endif

#ifdef __cplusplus
}
#endif

#endif
