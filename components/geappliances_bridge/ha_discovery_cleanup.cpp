#include "ha_discovery_cleanup.h"

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

#include <string.h>
#include <stdio.h>
#include <cstdlib>

#include "geappliances_bridge_log.h"

#ifdef USE_ESP_IDF_STUBS
#include "esp-idf/freertos_stub.h"
#include "esp-idf/esp_log.h"
#include "esp-idf/esp_heap_caps.h"
#else
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portable.h"
#endif

#ifdef HA_DISCOVERY_CLEANUP_TEST_EXPORT
#  define CLEANUP_FN
#else
#  define CLEANUP_FN static
#endif

GEA_TAG(TAG) = "ha_cleanup";

static void *cleanup_default_alloc(size_t size)
{
    return malloc(size);
}

static void cleanup_default_free(void *buffer)
{
    free(buffer);
}

void ha_discovery_cleanup_release_queue(ha_discovery_cleanup_t* self)
{
    if (self->topic_buf == NULL) return;
    void *buf = self->topic_buf;
    taskENTER_CRITICAL(&self->mux);
    self->topic_buf = NULL;
    taskEXIT_CRITICAL(&self->mux);
    if (self->free_fn != NULL) self->free_fn(buf);
    else free(buf);
}

CLEANUP_FN uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return 0;
    if (self->mqtt_client == NULL) return self->queue_count;

    if (self->topic_buf == NULL) return self->queue_count;

    if (self->retry_backoff_until_ms != 0 &&
        (int32_t)(self->get_time_ms() - self->retry_backoff_until_ms) < 0) {
        return self->queue_count;
    }

    char topic[256];
    uint16_t consumed;
    uint16_t remaining;

    taskENTER_CRITICAL(&self->mux);
    if (self->queue_count == 0) {
        taskEXIT_CRITICAL(&self->mux);
        return 0;
    }

    strncpy(topic, self->topic_buf, sizeof(topic) - 1);
    topic[sizeof(topic) - 1] = '\0';

    if (strlen(topic) != strlen(self->topic_buf)) {
        consumed = (uint16_t)(strlen(self->topic_buf) + 1);
        if (consumed > self->queue_write_pos) {
            consumed = self->queue_write_pos;
        }
        memmove(self->topic_buf, self->topic_buf + consumed,
                self->queue_write_pos - consumed);
        self->queue_write_pos -= consumed;
        self->queue_count--;
        remaining = self->queue_count;
        taskEXIT_CRITICAL(&self->mux);
        ESP_LOGW(TAG, "Topic truncated during flush, skipping");
        return remaining;
    }
    consumed = (uint16_t)(strlen(self->topic_buf) + 1);

    if (consumed > self->queue_write_pos) {
        consumed = self->queue_write_pos;
    }

    taskEXIT_CRITICAL(&self->mux);
    if (!mqtt_client_publish_raw(self->mqtt_client, topic, "", 0, true)) {
        self->retry_backoff_until_ms = self->get_time_ms()
            + HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS;
        ESP_LOGW(TAG, "Cleanup publish dropped, will retry: %s", topic);
        return self->queue_count;
    }

    self->retry_backoff_until_ms = 0;

    taskENTER_CRITICAL(&self->mux);
    if (self->queue_count == 0 || consumed > self->queue_write_pos) {
        taskEXIT_CRITICAL(&self->mux);
        ESP_LOGE(TAG, "Queue changed under the removal publish; skipping compaction");
        return self->queue_count;
    }
    memmove(self->topic_buf, self->topic_buf + consumed,
            self->queue_write_pos - consumed);
    self->queue_write_pos -= consumed;
    self->queue_count--;
    self->pass_removed_count++;
    remaining = self->queue_count;
    taskEXIT_CRITICAL(&self->mux);

    self->total_removed_count++;

    ESP_LOGD(TAG, "Removed old topic: %s", topic);

    return remaining;
}

CLEANUP_FN void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg)
{
    (void)payload;
    ha_discovery_cleanup_t* self = (ha_discovery_cleanup_t*)arg;

    if (self == NULL) return;

    uint32_t (*get_time)(void) = self->get_time_ms;
    if (get_time == NULL) return;

    size_t topic_len = strlen(topic);
    if (topic_len < 7) return;
    if (strcmp(topic + topic_len - 7, "/config") != 0) return;

    if (payload_len == 0) return;

    if (topic_len >= HA_CLEANUP_TOPIC_BUF_SIZE) {
        self->dropped_count++;
        return;
    }
    uint16_t needed = (uint16_t)(topic_len + 1);

    taskENTER_CRITICAL(&self->mux);
    self->pass_received_count++;

    if (self->topic_buf != NULL &&
        self->queue_write_pos + needed <= HA_CLEANUP_TOPIC_BUF_SIZE) {
        memcpy(self->topic_buf + self->queue_write_pos, topic, topic_len);
        self->topic_buf[self->queue_write_pos + topic_len] = '\0';
        self->queue_write_pos += needed;
        self->queue_count++;
    } else {
        self->dropped_count++;
    }

    self->pass_found_topics = true;
    self->last_activity_ms = get_time();
    taskEXIT_CRITICAL(&self->mux);
}

CLEANUP_FN void cleanup_start(ha_discovery_cleanup_t* self)
{
    self->queue_write_pos = 0;
    self->queue_count = 0;
    self->dropped_count = 0;
    self->flushed_once = false;
    self->clean_passes = 0;
    self->pass_found_topics = false;
    self->pass_received_count = 0;
    self->pass_removed_count = 0;
    self->subscribe_start_ms = 0;
    self->pass_number = 1;
    self->drain_start_ms = 0;
    self->retry_backoff_until_ms = 0;

    {
        size_t free_heap __attribute__((unused)) = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_free __attribute__((unused)) = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGV(TAG, "Heap before cleanup: free=%u, largest_block=%u, fragmentation=%.1f%%",
            (unsigned)free_heap, (unsigned)largest_free,
            (free_heap > 0) ? (1.0 - (double)largest_free / free_heap) * 100.0 : 0.0);
    }

    ESP_LOGI(TAG, "Starting HA discovery cleanup...");
}

void ha_discovery_cleanup_init(ha_discovery_cleanup_t* self)
{
    memset(self, 0, sizeof(*self));
    self->state = ha_cleanup_state_idle;
    self->alloc_fn = cleanup_default_alloc;
    self->free_fn = cleanup_default_free;
    portMUX_INITIALIZE(&self->mux);
}

void ha_discovery_cleanup_configure(ha_discovery_cleanup_t* self,
    const char* device_id, i_mqtt_client_t* mqtt_client, uint32_t (*get_time_ms)(void))
{
    self->device_id = device_id;
    self->mqtt_client = mqtt_client;
    self->get_time_ms = get_time_ms;
}

bool ha_discovery_cleanup_start(ha_discovery_cleanup_t* self)
{
    ha_discovery_cleanup_release_queue(self);

    void *(*alloc)(size_t) = self->alloc_fn != NULL ? self->alloc_fn : cleanup_default_alloc;
    char *buf = (char *) alloc(HA_CLEANUP_TOPIC_BUF_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Cleanup refused: could not allocate the %u-byte topic queue; "
                      "the existing discovery configs were left in place",
                 (unsigned) HA_CLEANUP_TOPIC_BUF_SIZE);
        return false;
    }

    self->state = ha_cleanup_state_cleaning;
    cleanup_start(self);

    taskENTER_CRITICAL(&self->mux);
    self->topic_buf = buf;
    taskEXIT_CRITICAL(&self->mux);
    return true;
}

void ha_discovery_cleanup_run(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return;
    if (self->mqtt_client == NULL) {
        self->state = ha_cleanup_state_done;
        ESP_LOGI(TAG, "Skipping cleanup (no MQTT client)");
        return;
    }
    if (self->device_id == NULL) {
        self->state = ha_cleanup_state_done;
        ESP_LOGW(TAG, "Skipping cleanup (no device_id)");
        return;
    }
    if (self->get_time_ms == NULL) {
        self->state = ha_cleanup_state_done;
        ESP_LOGW(TAG, "Skipping cleanup (no get_time_ms)");
        return;
    }

    if (!self->subscribed) {
        if (self->drain_start_ms != 0) {
            uint32_t now = self->get_time_ms();
            if (self->last_activity_ms > self->drain_start_ms) {
                self->drain_start_ms = self->last_activity_ms;
                return;
            }
            if (now - self->last_activity_ms < HA_CLEANUP_DRAIN_WAIT_MS) {
                return;
            }
            self->drain_start_ms = 0;
        }

        char sub_topic[128];
        snprintf(sub_topic, sizeof(sub_topic),
            "homeassistant/+/%s/#", self->device_id);
        mqtt_client_subscribe(self->mqtt_client, sub_topic,
            cleanup_topic_callback, self);
        self->subscribed = true;
        self->subscribe_start_ms = self->get_time_ms();
        self->last_activity_ms = self->subscribe_start_ms;
        self->pass_found_topics = false;
        self->pass_removed_count = 0;
        self->pass_received_count = 0;
        self->flushed_once = false;
        ESP_LOGI(TAG, "  Pass %u: subscribing to %s", self->pass_number, sub_topic);
        return;
    }

    uint32_t now = self->get_time_ms();

    if (now - self->subscribe_start_ms < HA_CLEANUP_MIN_SUBSCRIBE_MS) {
        cleanup_flush_queue(self);
        return;
    }

    if (!self->flushed_once) {
        cleanup_flush_queue(self);
        self->flushed_once = true;
        return;
    }

    if (now - self->last_activity_ms >= HA_CLEANUP_IDLE_TIMEOUT_MS) {
        cleanup_flush_queue(self);

        char sub_topic[128];
        snprintf(sub_topic, sizeof(sub_topic),
            "homeassistant/+/%s/#", self->device_id);
        mqtt_client_unsubscribe(self->mqtt_client, sub_topic);
        self->subscribed = false;
        self->drain_start_ms = self->last_activity_ms;

        if (self->pass_found_topics) {
            ESP_LOGI(TAG, "  Pass %u: %u received, %u removed, %u dropped — retrying",
                self->pass_number,
                self->pass_received_count,
                self->pass_removed_count,
                self->dropped_count);
            self->pass_number++;
            return;
        }

        ESP_LOGI(TAG, "  Pass %u: clean (%u received, %u removed)",
            self->pass_number,
            self->pass_received_count,
            self->pass_removed_count);
        self->clean_passes++;
        self->pass_number++;

        if (self->clean_passes < 2) {
            return;
        }

        taskENTER_CRITICAL(&self->mux);
        const uint16_t queued = self->queue_count;
        if (queued == 0) {
            self->state = ha_cleanup_state_done;
        }
        taskEXIT_CRITICAL(&self->mux);
        if (queued > 0) {
            ESP_LOGW(TAG, "  Pass %u: %u topics still queued for removal, retrying",
                self->pass_number, queued);
            return;
        }

        if (self->dropped_count > 0) {
            ESP_LOGW(TAG, "  Dropped %u topics due to buffer full during cleanup",
                (unsigned)self->dropped_count);
        }
        {
            size_t free_heap __attribute__((unused)) = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t largest_free __attribute__((unused)) = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            ESP_LOGV(TAG, "Heap after cleanup: free=%u, largest_block=%u, fragmentation=%.1f%%",
                (unsigned)free_heap, (unsigned)largest_free,
                (free_heap > 0) ? (1.0 - (double)largest_free / free_heap) * 100.0 : 0.0);
        }
        self->state = ha_cleanup_state_done;
        ESP_LOGI(TAG, "Cleanup complete");
        return;
    }

    cleanup_flush_queue(self);
}

void ha_discovery_cleanup_destroy(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return;

    self->get_time_ms = NULL;

    if (self->subscribed && self->mqtt_client != NULL) {
        i_mqtt_client_t* client = self->mqtt_client;
        self->mqtt_client = NULL;
        if (self->device_id != NULL) {
            char sub_topic[128];
            snprintf(sub_topic, sizeof(sub_topic),
                "homeassistant/+/%s/#", self->device_id);
            mqtt_client_unsubscribe(client, sub_topic);
        }
        self->subscribed = false;

        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ha_discovery_cleanup_release_queue(self);

    memset(self, 0, sizeof(*self));

    portMUX_INITIALIZE(&self->mux);
}
ha_cleanup_state_t ha_discovery_cleanup_get_state(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return ha_cleanup_state_done;
    return self->state;
}

bool ha_discovery_cleanup_is_done(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return true;
    return self->state == ha_cleanup_state_done;
}

uint32_t ha_discovery_cleanup_total_removed(const ha_discovery_cleanup_t* self)
{
    return self->total_removed_count;
}
