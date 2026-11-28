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

enum { ERD_MQTT_PUBLISHER_TASK_STACK_BYTES = 4096 };

typedef struct {
  erd_cache_t* cache;
  i_mqtt_client_t* mqtt_client;
  const char* device_id;
  uint8_t primary_board_address;
  uint16_t publish_index;
  uint32_t retry_backoff_until_ms;
  uint32_t last_work_signal_ms;
  bool mqtt_connected;
  bool paused;
  bool first_round_done;
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t mqtt_connect_subscription;
  uint32_t total_published;
  uint32_t missed_loops;
  uint32_t publish_count_window;
  uint32_t (*get_time_ms)(void);
  uint32_t disconnect_start_ms;
  uint32_t disconnect_count;
  uint32_t last_disconnect_duration_ms;
  TaskHandle_t    task_handle;
  StaticTask_t    task_tcb;
  StackType_t     task_stack[ERD_MQTT_PUBLISHER_TASK_STACK_BYTES / sizeof(StackType_t)];
  SemaphoreHandle_t work_semaphore;
  SemaphoreHandle_t state_mutex;
  SemaphoreHandle_t done_semaphore;
  bool task_running;
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

bool erd_cache_mqtt_publisher_publish_one(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_pause(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_resume(erd_cache_mqtt_publisher_t* self);

void erd_cache_mqtt_publisher_set_time_fn(
  erd_cache_mqtt_publisher_t* self,
  uint32_t (*get_time_ms)(void));

uint32_t erd_cache_mqtt_publisher_get_publish_rate(erd_cache_mqtt_publisher_t* self);
bool erd_cache_mqtt_publisher_first_round_done(erd_cache_mqtt_publisher_t* self);

uint32_t erd_cache_mqtt_publisher_get_disconnect_count(erd_cache_mqtt_publisher_t* self);

uint32_t erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(erd_cache_mqtt_publisher_t* self);

#ifdef __cplusplus
}
#endif

#endif
