/**
 * Independent of the discovery manager — no knowledge of discovery state
 * or buffers.
 */

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

/* Wait after a dropped removal publish before trying again. Same storm this
 * removed from the cache publisher (#183) and from the discovery publish path
 * (HA_DISCOVERY_PUBLISH_RETRY_BACKOFF_MS in ha_discovery_manager.cpp): the
 * topic stays queued, so retrying at main-loop rate against a full outbound
 * ring only churns the backend's drop counter. Sized like the discovery
 * window -- the phase timeout that bounds it is an idle timeout on progress,
 * so this trades attempt rate against convergence time only. */
#define HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS 64

/* Overridable for tests. */

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

  char topic_buf[HA_CLEANUP_TOPIC_BUF_SIZE];
  uint16_t queue_write_pos;
  uint16_t queue_count;
  uint16_t dropped_count;
  /* Topics removed across the whole run, never reset by a pass, so a caller
   * can tell a slow-but-progressing cleanup from a stalled one -- the
   * lifecycle phase machine uses it as its cleanup-progress signal. */
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
  /* Timestamp until which a dropped removal publish suppresses further
   * attempts; 0 = no window armed. Main-loop only, never touched by the MQTT
   * callback, so mux does not guard it. */
  uint32_t retry_backoff_until_ms;

  /* Critical section mux — per-instance, protects shared state between
   * the MQTT task callback and the main loop. */
  portMUX_TYPE mux;
} ha_discovery_cleanup_t;

void ha_discovery_cleanup_init(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_configure(ha_discovery_cleanup_t* self,
    const char* device_id, i_mqtt_client_t* mqtt_client, uint32_t (*get_time_ms)(void));

void ha_discovery_cleanup_start(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_run(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_destroy(ha_discovery_cleanup_t* self);

ha_cleanup_state_t ha_discovery_cleanup_get_state(ha_discovery_cleanup_t* self);

bool ha_discovery_cleanup_is_done(ha_discovery_cleanup_t* self);

/* Topics removed successfully so far. Survives the per-pass counters; only
 * init()/destroy() zero it. */
uint32_t ha_discovery_cleanup_total_removed(const ha_discovery_cleanup_t* self);

#ifdef HA_DISCOVERY_CLEANUP_TEST_EXPORT
void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg);
uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self);
void cleanup_start(ha_discovery_cleanup_t* self);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ha_discovery_cleanup_h */
