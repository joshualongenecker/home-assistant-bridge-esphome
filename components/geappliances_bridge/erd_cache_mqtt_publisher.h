/* With the ESP-IDF framework, publishing runs in a FreeRTOS background task
 * to avoid blocking the ESPHome main loop on the IDF MQTT mutex. */

#ifndef erd_cache_mqtt_publisher_h
#define erd_cache_mqtt_publisher_h

#include <stdint.h>
#include <stdbool.h>

#include "erd_cache.h"
#include "i_mqtt_client.h"
#include "i_tiny_event.h"

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif
#ifdef USE_ESP_IDF_STUBS
  #include "esp-idf/freertos_stub.h"
#else
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #include "freertos/queue.h"
#endif

/*
 * mqtt_publisher_task() calls through ESPHome's MQTT client, which may add a
 * substantial IDF call stack while the broker queue is congested.  Keep this
 * separate from its embedded buffers: those buffers live in the bridge object
 * and do not consume the task stack.
 */
enum { ERD_MQTT_PUBLISHER_TASK_STACK_BYTES = 4096 };

typedef struct {
  erd_cache_t* cache;              // Shared cache (owned by GeappliancesBridge)
  i_mqtt_client_t* mqtt_client;
  const char* device_id;
  uint8_t primary_board_address;  /* board address that maps to the legacy (no-prefix) MQTT topic */
  uint16_t publish_index;
  bool mqtt_connected;
  bool paused;
  bool first_round_done;
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t mqtt_connect_subscription;
  uint32_t total_published;
  uint32_t missed_loops;
  uint32_t publish_count_window;
  uint32_t (*get_time_ms)(void);
  uint32_t disconnect_start_ms;  /* millis() when MQTT disconnected; 0 if connected */
  uint32_t disconnect_count;
  uint32_t last_disconnect_duration_ms;
  TaskHandle_t    task_handle;
  StaticTask_t    task_tcb;
  StackType_t     task_stack[ERD_MQTT_PUBLISHER_TASK_STACK_BYTES / sizeof(StackType_t)];
  SemaphoreHandle_t work_semaphore;
  SemaphoreHandle_t state_mutex;  // Protects shared state from torn reads during context switches
  SemaphoreHandle_t done_semaphore; // Task gives this before exiting (clean shutdown handshake)
  bool task_running;
  // Pre-allocated buffers for the background task to avoid stack overflow.
  char task_topic[128];
  char task_hex[512];
} erd_cache_mqtt_publisher_t;

#ifdef __cplusplus
extern "C" {
#endif

void erd_cache_mqtt_publisher_init(
  erd_cache_mqtt_publisher_t* self,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client,
  const char* device_id,
  uint8_t primary_board_address);

void erd_cache_mqtt_publisher_destroy(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_start(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_stop(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_signal_work(erd_cache_mqtt_publisher_t* self);

/*!
 * Publishes at most one updated cache entry: snapshot, topic/hex formatting,
 * retained publish, and publish bookkeeping. No-ops (incrementing missed_loops)
 * if MQTT is disconnected or publishing is paused. The background task calls
 * this once per wake; exposed for tests that drive the publish path directly.
 */
bool erd_cache_mqtt_publisher_publish_one(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self);

/*!
 * Temporarily pause publishing.
 * Use during HA discovery cleanup to reduce MQTT queue contention.
 */
void erd_cache_mqtt_publisher_pause(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_resume(erd_cache_mqtt_publisher_t* self);

/*!
 * Override the time source (defaults to esphome::millis).
 * Useful for testing.
 */
void erd_cache_mqtt_publisher_set_time_fn(
  erd_cache_mqtt_publisher_t* self,
  uint32_t (*get_time_ms)(void));

/*!
 * Returns the number of ERD publishes in the last 60 seconds, then resets the window.
 */
uint32_t erd_cache_mqtt_publisher_get_publish_rate(erd_cache_mqtt_publisher_t* self);
/*!
 * Returns true if the publisher has completed a full cache round since the
 * last resume.  Thread-safe — acquires the state mutex with the ESP-IDF framework.
 */
bool erd_cache_mqtt_publisher_first_round_done(erd_cache_mqtt_publisher_t* self);

/*!
 * Returns the total number of MQTT disconnects since init.
 * Thread-safe — acquires the state mutex with the ESP-IDF framework.
 */
uint32_t erd_cache_mqtt_publisher_get_disconnect_count(erd_cache_mqtt_publisher_t* self);

/*!
 * Returns the duration of the last MQTT disconnect in milliseconds.
 * Thread-safe — acquires the state mutex with the ESP-IDF framework.
 */
uint32_t erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(erd_cache_mqtt_publisher_t* self);

#ifdef __cplusplus
}
#endif

#endif
