/*!
 * @file
 * @brief ERD cache MQTT publisher implementation.
 */

#include "erd_cache_mqtt_publisher.h"
#include "erd_cache.h"
#include "erd_bridge_common.h"
#include "geappliances_bridge_log.h"
#include "i_mqtt_client.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/mqtt/mqtt_client.h"

#include <cstdio>
#include <string.h>


#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif
GEA_TAG(PUBLISHER_TAG) = "erd_cache_mqtt_publisher";
#include "esp_task_wdt.h"

static void mqtt_publisher_task(void* arg)
{
  erd_cache_mqtt_publisher_t* self = (erd_cache_mqtt_publisher_t*)arg;

  // Defensive: if semaphore creation failed, exit immediately.
  if (self->work_semaphore == NULL) {
    vTaskDelete(NULL);
    return;
  }

  while (self->task_running) {
    // Block until the main loop signals work. No timeout —
    // the main loop controls pacing.
    if (xSemaphoreTake(self->work_semaphore, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    // Acquire mutex to safely read shared state (mqtt_connected, cache pointers,
    // publish_index) and protect the entire drain loop. These fields can be
    // modified by the main loop during preemptive context switches. The mutex
    // protects against interleaving on the same core — cross-core parallelism
    // is prevented by pinning both tasks to Core 1 (dual-core) or by
    // single-core hardware (C3, C6).
    bool connected = false;
    bool has_deps = false;
    bool paused = false;
    bool mutex_held = false;
    if (self->state_mutex) {
      if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        mutex_held = true;
        connected = self->mqtt_connected;
        paused = self->paused;
        has_deps = self->cache != NULL && self->mqtt_client != NULL &&
                   self->device_id != NULL && self->get_time_ms != NULL;
      }
    } else {
      // Fallback when mutex creation failed — read without protection.
      connected = self->mqtt_connected;
      paused = self->paused;
      has_deps = self->cache != NULL && self->mqtt_client != NULL &&
                 self->device_id != NULL && self->get_time_ms != NULL;
    }

    if (!connected || paused || !has_deps) {
      if (mutex_held) {
        xSemaphoreGive(self->state_mutex);
      }
      continue;
    }

    /* Publish one entry per wake. The 100ms semaphore timeout provides
     * natural pacing. The mutex is held throughout to protect
     * publish_index and cache access. */
    erd_cache_entry_t* entry = erd_cache_get_next_updated(self->cache, &self->publish_index);
    if (entry) {
      const uint8_t* data = erd_cache_entry_data(self->cache, entry);

      int topic_len;
      if (entry->board_address == PROBE_ENTRY_DEFAULT_ADDRESS) {
        topic_len = snprintf(self->task_topic, sizeof(self->task_topic), "geappliances/%s/erd/0x%04x/value", self->device_id, entry->erd);
      } else {
        topic_len = snprintf(self->task_topic, sizeof(self->task_topic), "geappliances/%s/erd/0x%02x_0x%04x/value", self->device_id, entry->board_address, entry->erd);
      }
      if (topic_len >= 0 && (unsigned)topic_len < sizeof(self->task_topic)) {
        size_t data_len = entry->data_size;
        for (size_t i = 0; i < data_len; i++) {
          snprintf(self->task_hex + i * 2, 3, "%02x", data[i]);
        }
        self->task_hex[data_len * 2] = '\0';

        uint32_t t_publish = self->get_time_ms();
        bool sent = mqtt_client_publish_raw(self->mqtt_client, self->task_topic,
            self->task_hex, data_len * 2, true);
        uint32_t elapsed = self->get_time_ms() - t_publish;

        if (elapsed >= 1000) {
          ESP_LOGW(PUBLISHER_TAG, "Slow publish: %lums for ERD 0x%04x addr 0x%02x", (unsigned long)elapsed, entry->erd, entry->board_address);
        }

        if (sent) {
          erd_cache_mark_published(self->cache, entry);
          self->total_published++;
          self->publish_count_window++;
        } else {
          /* Publish dropped (queue full or not connected) — re-set
           * update_required so the entry is picked up on the next wake. */
          erd_cache_mark_unpublished(self->cache, entry);
        }
      } else if (topic_len >= (int)sizeof(self->task_topic)) {
        ESP_LOGW(PUBLISHER_TAG, "MQTT topic truncated (device_id too long: %s)", self->device_id);
      }
    } else if (!self->first_round_done) {
      /* Scanned full cache with no pending entries — first round is done. */
      self->first_round_done = true;
    }

    if (mutex_held) {
      xSemaphoreGive(self->state_mutex);
    }
  }

  // Signal completion before deleting the task (clean shutdown handshake).
  if (self->done_semaphore) {
    xSemaphoreGive(self->done_semaphore);
  }
  vTaskDelete(NULL);
}

void erd_cache_mqtt_publisher_init(
  erd_cache_mqtt_publisher_t* self,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client,
  const char* device_id)
{
  memset(self, 0, sizeof(*self));
  self->cache = cache;
  self->mqtt_client = mqtt_client;
  self->device_id = device_id;
  self->publish_index = 0;
  self->mqtt_connected = false;
  self->get_time_ms = esphome::millis;

  self->work_semaphore = xSemaphoreCreateBinary();
  if (!self->work_semaphore) {
    ESP_LOGE(PUBLISHER_TAG, "Failed to create work semaphore");
  }
  self->state_mutex = xSemaphoreCreateMutex();
  if (!self->state_mutex) {
    ESP_LOGE(PUBLISHER_TAG, "Failed to create state mutex");
  }
  self->done_semaphore = xSemaphoreCreateBinary();
  if (!self->done_semaphore) {
    ESP_LOGE(PUBLISHER_TAG, "Failed to create done semaphore");
  }
  self->task_running = false;

  if (!mqtt_client) return;
  /* Subscribe to MQTT disconnect event */
  tiny_event_subscription_init(
    &self->mqtt_disconnect_subscription, self,
    +[](void* context, const void*) {
      erd_cache_mqtt_publisher_on_disconnected(
        reinterpret_cast<erd_cache_mqtt_publisher_t*>(context));
    });
  tiny_event_subscribe(
    mqtt_client_on_mqtt_disconnect(self->mqtt_client),
    &self->mqtt_disconnect_subscription);

  /* Subscribe to MQTT connect event */
  tiny_event_subscription_init(
    &self->mqtt_connect_subscription, self,
    +[](void* context, const void*) {
      erd_cache_mqtt_publisher_on_connected(
        reinterpret_cast<erd_cache_mqtt_publisher_t*>(context));
    });
  tiny_event_subscribe(
    mqtt_client_on_mqtt_connect(self->mqtt_client),
    &self->mqtt_connect_subscription);

  /* If MQTT is already connected when we register, set the flag so the
   * publisher knows it can publish.  Don't call on_connected() — that
   * logs "MQTT reconnected" which is misleading at init time. */
  auto global = esphome::mqtt::global_mqtt_client;
  if (global != nullptr && global->is_connected()) {
    self->mqtt_connected = true;
  }

  ESP_LOGI(PUBLISHER_TAG, "ERD cache MQTT publisher initialized with device ID: %s", self->device_id);
}

void erd_cache_mqtt_publisher_destroy(erd_cache_mqtt_publisher_t* self)
{
  erd_cache_mqtt_publisher_stop(self);

  if (!self->mqtt_client) {
    memset(self, 0, sizeof(*self));
    return;
  }

  tiny_event_unsubscribe(
    mqtt_client_on_mqtt_disconnect(self->mqtt_client),
    &self->mqtt_disconnect_subscription);
  tiny_event_unsubscribe(
    mqtt_client_on_mqtt_connect(self->mqtt_client),
    &self->mqtt_connect_subscription);

  if (self->work_semaphore) {
    vSemaphoreDelete(self->work_semaphore);
    self->work_semaphore = NULL;
  }
  if (self->state_mutex) {
    vSemaphoreDelete(self->state_mutex);
    self->state_mutex = NULL;
  }
  if (self->done_semaphore) {
    vSemaphoreDelete(self->done_semaphore);
    self->done_semaphore = NULL;
  }

  memset(self, 0, sizeof(*self));
}

void erd_cache_mqtt_publisher_start(erd_cache_mqtt_publisher_t* self)
{
  if (self->task_handle != NULL) return; // already running
  if (self->work_semaphore == NULL) return; // semaphore creation failed in init
  self->task_running = true;
  /* Pin the publisher task to the same core as ESPHome's main loop.
   * ESPHome pins its loop task to Core 1 on dual-core ESP32
   * (esphome/components/esp32/core.cpp: xTaskCreateStaticPinnedToCore(..., 1)).
   * On dual-core ESP32-S3 the erd_cache_t is accessed from both the
   * main loop and this task.  The cache has no mutex — thread safety
   * relies on single-core ordering (tick → signal_work → drain).
   * Running on the same core as the main loop restores that guarantee.
   * On single-core chips (C3, C6) the coreID is ignored. */
#if CONFIG_FREERTOS_UNICORE
  self->task_handle = xTaskCreateStatic(
      mqtt_publisher_task,
      "erd_mqtt_pub",
      ERD_MQTT_PUBLISHER_TASK_STACK_BYTES / sizeof(StackType_t),  /* words, matching task_stack[] size */
      self,
      2,
      self->task_stack,
      &self->task_tcb);
#else
  self->task_handle = xTaskCreateStaticPinnedToCore(
      mqtt_publisher_task,
      "erd_mqtt_pub",
      ERD_MQTT_PUBLISHER_TASK_STACK_BYTES / sizeof(StackType_t),  /* words, matching task_stack[] size */
      self,
      2,
      self->task_stack,
      &self->task_tcb,
      1);  /* Core 1 — same core as ESPHome's main loop task */
#endif
  if (self->task_handle == NULL) {
    ESP_LOGE(PUBLISHER_TAG, "Failed to create MQTT publisher task");
    self->task_running = false;
  }
}

void erd_cache_mqtt_publisher_stop(erd_cache_mqtt_publisher_t* self)
{
  if (!self->task_running) return;
  /* Signal the semaphore first to wake the task, then set
   * task_running=false so the task sees the flag on wake. */
  if (self->work_semaphore != NULL) {
    xSemaphoreGive(self->work_semaphore);
  }
  self->task_running = false;
  // Wait for the task to signal completion via done_semaphore.
  // The task gives this semaphore before calling vTaskDelete, so we
  // know it has entered the termination path.
  if (self->done_semaphore != NULL) {
    if (xSemaphoreTake(self->done_semaphore, pdMS_TO_TICKS(1000)) != pdTRUE) {
      ESP_LOGW(PUBLISHER_TAG, "MQTT publisher task did not signal done within 1 s");
    }
  } else {
    // Fallback: poll with delay when done_semaphore creation failed.
    uint32_t start = esphome::millis();
    while (self->task_handle != NULL && esphome::millis() - start < 1000) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (self->task_handle != NULL) {
      ESP_LOGW(PUBLISHER_TAG, "MQTT publisher task did not terminate within 1 s");
    }
  }

  // After vTaskDelete() the task is on xTasksWaitingTermination.
  // The idle task runs prvCheckTasksWaitingTermination to unlink the
  // TCB's list items via uxListRemove().  For StaticTask_t the TCB
  // memory is caller-owned so FreeRTOS doesn't free it, but it does
  // access the TCB's list pointers during unlinking.  We MUST yield
  // here so the idle task can finish before destroy() memsets the
  // struct — which would zero those list pointers mid-uxListRemove.
  esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(100));
  self->task_handle = NULL;
}

void erd_cache_mqtt_publisher_signal_work(erd_cache_mqtt_publisher_t* self)
{
  if (self->work_semaphore != NULL) {
    // Non-blocking give — if task is already waiting, it will wake up.
    xSemaphoreGive(self->work_semaphore);
  }
}

bool erd_cache_mqtt_publisher_loop(erd_cache_mqtt_publisher_t* self)
{
  if (!self->cache || !self->mqtt_client || !self->device_id || !self->get_time_ms) {
    return false;
  }

  if (!self->mqtt_connected) {
    self->missed_loops++;
    return false;
  }

  erd_cache_entry_t* entry = erd_cache_get_next_updated(self->cache, &self->publish_index);
  if (!entry) {
    if (!self->first_round_done) {
      self->first_round_done = true;
    }
    return false;
  }

  const uint8_t* data = erd_cache_entry_data(self->cache, entry);

  char topic[128];
  int topic_len;
  if (entry->board_address == PROBE_ENTRY_DEFAULT_ADDRESS) {
    topic_len = snprintf(topic, sizeof(topic), "geappliances/%s/erd/0x%04x/value", self->device_id, entry->erd);
  } else {
    topic_len = snprintf(topic, sizeof(topic), "geappliances/%s/erd/0x%02x_0x%04x/value", self->device_id, entry->board_address, entry->erd);
  }
  if (topic_len < 0 || (unsigned)topic_len >= sizeof(topic)) {
    ESP_LOGW(PUBLISHER_TAG, "MQTT topic truncated (device_id too long: %s)", self->device_id);
    return false;
  }

  char hex[512];
  size_t data_len = entry->data_size;
  for (size_t i = 0; i < data_len; i++) {
    snprintf(hex + i * 2, 3, "%02x", data[i]);
  }
  hex[data_len * 2] = '\0';

  uint32_t t_publish = self->get_time_ms();
  bool sent = mqtt_client_publish_raw(self->mqtt_client, topic, hex, data_len * 2, true);
  uint32_t elapsed = self->get_time_ms() - t_publish;

  if (elapsed >= 1000) {
    ESP_LOGW(PUBLISHER_TAG, "Slow publish: %lums for ERD 0x%04x addr 0x%02x", (unsigned long)elapsed, entry->erd, entry->board_address);
  }

  if (sent) {
    erd_cache_mark_published(self->cache, entry);
    self->total_published++;
    self->publish_count_window++;
  } else {
    erd_cache_mark_unpublished(self->cache, entry);
  }

  return true;
}

void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self)
{
  /* Threshold for considering a disconnect "long enough" to warrant a full
   * republish of all cached ERDs.  A 60 s gap covers broker restarts that
   * lose their in-memory retained store before the next autosave flush,
   * while avoiding unnecessary republishes on short network blips. */
  static const uint32_t RECONNECT_REPUBLISH_THRESHOLD_MS = 60000;

  bool was_long_disconnect = false;
  uint32_t disconnect_start = 0;

  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      self->mqtt_connected = true;
      self->first_round_done = false;
      disconnect_start = self->disconnect_start_ms;
      /* Don't reset disconnect_start_ms here — it may be set from a prior
       * disconnect and we want to measure the cumulative outage duration
       * across multiple ESPHome reconnect attempts. Reset only on the final
       * successful reconnect (when we know we're stable). */
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    self->mqtt_connected = true;
    self->first_round_done = false;
    disconnect_start = self->disconnect_start_ms;
  }

  uint32_t now = self->get_time_ms ? self->get_time_ms() : 0;
  uint32_t duration = (disconnect_start != 0) ? (now - disconnect_start) : 0;

  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      self->last_disconnect_duration_ms = duration;
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    self->last_disconnect_duration_ms = duration;
  }

  if (disconnect_start != 0 && duration >= RECONNECT_REPUBLISH_THRESHOLD_MS) {
    was_long_disconnect = true;
  }

  if (was_long_disconnect) {
    ESP_LOGI(PUBLISHER_TAG, "MQTT reconnected after %lu s — republishing all cached ERDs",
             (unsigned long)((now - disconnect_start) / 1000));
    erd_cache_mark_all_updated(self->cache);
    if (self->state_mutex) {
      if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        self->publish_index = 0;
        xSemaphoreGive(self->state_mutex);
      }
    } else {
      self->publish_index = 0;
    }
  } else {
    ESP_LOGI(PUBLISHER_TAG, "MQTT reconnected — resuming ERD cache publishing");
  }
  /* Wake the background task so it can start publishing again. */
  erd_cache_mqtt_publisher_signal_work(self);
}
void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self)
{
  uint32_t now = self->get_time_ms ? self->get_time_ms() : 0;

  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      bool was_connected = self->mqtt_connected;
      self->mqtt_connected = false;
      if (was_connected || self->disconnect_start_ms == 0) {
        self->disconnect_start_ms = now;
      }
      self->disconnect_count++;
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    bool was_connected = self->mqtt_connected;
    self->mqtt_connected = false;
    if (was_connected || self->disconnect_start_ms == 0) {
      self->disconnect_start_ms = now;
    }
    self->disconnect_count++;
  }
  ESP_LOGW(PUBLISHER_TAG, "MQTT disconnected — pausing ERD cache publishing");
}

void erd_cache_mqtt_publisher_pause(erd_cache_mqtt_publisher_t* self)
{
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      self->paused = true;
      self->first_round_done = false;
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    self->paused = true;
    self->first_round_done = false;
  }
}

void erd_cache_mqtt_publisher_resume(erd_cache_mqtt_publisher_t* self)
{
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      self->paused = false;
      xSemaphoreGive(self->state_mutex);
      // Wake the task so it can resume publishing.
      erd_cache_mqtt_publisher_signal_work(self);
    }
  } else {
    self->paused = false;
  }
}

void erd_cache_mqtt_publisher_set_time_fn(
  erd_cache_mqtt_publisher_t* self,
  uint32_t (*get_time_ms)(void))
{
  self->get_time_ms = get_time_ms;
}

uint32_t erd_cache_mqtt_publisher_get_publish_rate(erd_cache_mqtt_publisher_t* self)
{
  uint32_t count = 0;
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      count = self->publish_count_window;
      self->publish_count_window = 0;
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    count = self->publish_count_window;
    self->publish_count_window = 0;
  }
  return count;
}

bool erd_cache_mqtt_publisher_first_round_done(erd_cache_mqtt_publisher_t* self)
{
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      bool done = self->first_round_done;
      xSemaphoreGive(self->state_mutex);
      return done;
    }
  }
  return self->first_round_done;
}

uint32_t erd_cache_mqtt_publisher_get_disconnect_count(erd_cache_mqtt_publisher_t* self)
{
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      uint32_t count = self->disconnect_count;
      xSemaphoreGive(self->state_mutex);
      return count;
    }
  }
  return self->disconnect_count;
}

uint32_t erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(erd_cache_mqtt_publisher_t* self)
{
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      uint32_t duration = self->last_disconnect_duration_ms;
      xSemaphoreGive(self->state_mutex);
      return duration;
    }
  }
  return self->last_disconnect_duration_ms;
}
