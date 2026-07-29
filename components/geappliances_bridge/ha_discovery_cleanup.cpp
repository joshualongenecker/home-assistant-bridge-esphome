/**
 * HA Discovery Cleanup Module.
 * Discovers and removes old Home Assistant MQTT discovery topics for a device.
 * Independent of the discovery manager — no knowledge of discovery state or
 * buffers.
 */

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

/* ------------------------------------------------------------------ */
/* Cleanup: discover and remove old HA discovery topics               */
/* ------------------------------------------------------------------ */

/* Uses a single wildcard subscription (homeassistant/+/{device_id}/#)
 * to catch all retained discovery topics across all domains at once. */

/* Idle timeout after last topic callback during cleanup.
 * The ESP-IDF framework MQTT inbound queue holds ~32 messages before dropping.
 * This must be long enough for the broker to finish delivering a batch
 * and for the MQTT task to process its queue before we flush. */

/* Minimum time we stay subscribed before considering a pass complete.
 * Ensures we wait for the initial retained message burst even if
 * cleanup_run() isn't called frequently. */

/* Wait after unsubscribe for the inbound MQTT event queue to drain
 * before re-subscribing. If no new topic callbacks fire during this
 * window, the queue is empty and it's safe to re-subscribe. */

/* Flush one topic per batch call. Each publish allocates a std::string
 * on the heap; processing one at a time minimizes peak heap pressure. */

/* Expose cleanup functions for unit testing. */
#ifdef HA_DISCOVERY_CLEANUP_TEST_EXPORT
#  define CLEANUP_FN
#else
#  define CLEANUP_FN static
#endif

GEA_TAG(TAG) = "ha_cleanup";

/* Critical section mux for protecting shared state between the MQTT task
 * callback and the main loop. ESP-IDF 5.5.5 requires a portMUX_TYPE* on
 * the Xtensa port; passing it on RISC-V is harmless. */
static portMUX_TYPE cleanup_mux = portMUX_INITIALIZER_UNLOCKED;

/* Flush queued cleanup topics: publish empty retained payloads to remove them.
 * Called from cleanup_run() during idle periods, not from the MQTT callback,
 * to avoid blocking the ESP-IDF framework MQTT task. Returns the number of topics
 * remaining in the queue (0 means all flushed). */
CLEANUP_FN uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return 0;
    if (self->mqtt_client == NULL) return self->queue_count;

    /* topic[] is 256 bytes. Topics from the wildcard subscription are bounded
     * by HA_CLEANUP_TOPIC_BUF_SIZE entries in topic_buf. Each topic is at most
     * ~200 bytes (homeassistant/{domain}/{device_id}/{entity_id}/config).
     * strncpy truncates safely. Long-term: validate topic length before copy. */
    char topic[256];
    uint16_t consumed;
    uint16_t remaining;

    taskENTER_CRITICAL(&cleanup_mux);
    if (self->queue_count == 0) {
        taskEXIT_CRITICAL(&cleanup_mux);
        return 0;
    }

    /* Copy the topic string to a stack buffer BEFORE compacting.
     * memmove shifts data forward into topic_buf, invalidating any
     * pointer into the buffer. */
    strncpy(topic, self->topic_buf, sizeof(topic) - 1);
    topic[sizeof(topic) - 1] = '\0';

    /* Detect truncation: if the topic was longer than our stack buffer,
     * skip it to avoid publishing to a malformed topic. */
    if (strlen(topic) != strlen(self->topic_buf)) {
        ESP_LOGW(TAG, "Topic truncated during flush, skipping");
        consumed = (uint16_t)(strlen(self->topic_buf) + 1);
        if (consumed > self->queue_write_pos) {
            consumed = self->queue_write_pos;
        }
        memmove(self->topic_buf, self->topic_buf + consumed,
                self->queue_write_pos - consumed);
        self->queue_write_pos -= consumed;
        self->queue_count--;
        remaining = self->queue_count;
        taskEXIT_CRITICAL(&cleanup_mux);
        return remaining;
    }
    consumed = (uint16_t)(strlen(self->topic_buf) + 1);

    /* Safety clamp: prevent underflow if buffer is corrupted. */
    if (consumed > self->queue_write_pos) {
        consumed = self->queue_write_pos;
    }

    /* Compact: shift remaining data to front. Save pre-compact state
     * so we can undo if the publish fails. */
    uint16_t saved_write_pos = self->queue_write_pos;
    uint16_t saved_count = self->queue_count;
    memmove(self->topic_buf, self->topic_buf + consumed,
            self->queue_write_pos - consumed);
    self->queue_write_pos -= consumed;
    self->queue_count--;
    self->pass_removed_count++;
    taskEXIT_CRITICAL(&cleanup_mux);

    if (!mqtt_client_publish_raw(self->mqtt_client, topic, "", 0, true)) {
        /* Publish dropped (queue full) — undo the compact using saved
         * state. The saved values were captured inside the critical
         * section so they are consistent even if the callback fires
         * during the publish attempt. */
        taskENTER_CRITICAL(&cleanup_mux);
        self->queue_write_pos = saved_write_pos;
        self->queue_count = saved_count;
        self->pass_removed_count--;
        remaining = self->queue_count;
        taskEXIT_CRITICAL(&cleanup_mux);
        ESP_LOGW(TAG, "Cleanup publish dropped, will retry: %s", topic);
        return remaining;
    }

    ESP_LOGD(TAG, "Removed old topic: %s", topic);

    /* Read remaining count after successful publish. */
    taskENTER_CRITICAL(&cleanup_mux);
    remaining = self->queue_count;
    taskEXIT_CRITICAL(&cleanup_mux);

    return remaining;
}

/* Callback for homeassistant/+/{device_id}/# wildcard subscription during cleanup.
 * Stores the full topic string in the buffer for republishing from the main loop.
 * Keeps the callback short — no outbound publish call — so the MQTT task's
 * inbound queue drains fast and retained message bursts don't overflow.
 *
 * THREAD SAFETY: This callback runs in the ESP-IDF framework MQTT task context (a separate
 * FreeRTOS task). Shared state (topic_buf, queue_write_pos, queue_count) is
 * protected by taskENTER_CRITICAL()/taskEXIT_CRITICAL(). This is safe on
 * single-core ESP32-C3 where critical sections disable interrupts. On dual-core
 * ESP32, a mutex would be needed instead. The code assumes single-core. */
CLEANUP_FN void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg)
{
    (void)payload;
    ha_discovery_cleanup_t* self = (ha_discovery_cleanup_t*)arg;

    /* Guard against callback firing after destroy (memset zeroes struct). */
    if (self == NULL || self->get_time_ms == NULL) return;

    /* Only remove config topics. */
    size_t topic_len = strlen(topic);
    if (topic_len < 7) return;
    if (strcmp(topic + topic_len - 7, "/config") != 0) return;

    /* If the payload is empty, it's our own echo from a previous clear — skip. */
    if (payload_len == 0) return;

    /* Guard against oversized topics: if topic_len >= HA_CLEANUP_TOPIC_BUF_SIZE,
     * the uint16_t cast of (topic_len + 1) could overflow to 0. */
    if (topic_len >= HA_CLEANUP_TOPIC_BUF_SIZE) {
        self->dropped_count++;
        return;
    }
    uint16_t needed = (uint16_t)(topic_len + 1);

    taskENTER_CRITICAL(&cleanup_mux);
    /* Diagnostic: count all callbacks received (inside critical section to avoid race). */
    self->pass_received_count++;

    /* Check if buffer has room. Simple linear append — no ring buffer. */
    if (self->queue_write_pos + needed <= HA_CLEANUP_TOPIC_BUF_SIZE) {
        memcpy(self->topic_buf + self->queue_write_pos, topic, topic_len);
        self->topic_buf[self->queue_write_pos + topic_len] = '\0';
        self->queue_write_pos += needed;
        self->queue_count++;
    } else {
        /* Buffer full — drop this topic */
        self->dropped_count++;
    }

    self->pass_found_topics = true;
    self->last_activity_ms = self->get_time_ms();
    taskEXIT_CRITICAL(&cleanup_mux);
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

    /* Heap fragmentation baseline before cleanup. */
    {
        size_t free_heap __attribute__((unused)) = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_free __attribute__((unused)) = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGV(TAG, "Heap before cleanup: free=%u, largest_block=%u, fragmentation=%.1f%%",
            (unsigned)free_heap, (unsigned)largest_free,
            (free_heap > 0) ? (1.0 - (double)largest_free / free_heap) * 100.0 : 0.0);
    }

    ESP_LOGI(TAG, "Starting HA discovery cleanup...");
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void ha_discovery_cleanup_init(ha_discovery_cleanup_t* self)
{
    memset(self, 0, sizeof(*self));
    self->state = ha_cleanup_state_idle;
}

void ha_discovery_cleanup_configure(ha_discovery_cleanup_t* self,
    const char* device_id, i_mqtt_client_t* mqtt_client, uint32_t (*get_time_ms)(void))
{
    self->device_id = device_id;
    self->mqtt_client = mqtt_client;
    self->get_time_ms = get_time_ms;
}

void ha_discovery_cleanup_start(ha_discovery_cleanup_t* self)
{
    self->state = ha_cleanup_state_cleaning;
    cleanup_start(self);
}

void ha_discovery_cleanup_run(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return;
    if (self->mqtt_client == NULL) {
        /* No MQTT client — skip cleanup, mark done. */
        self->state = ha_cleanup_state_done;
        ESP_LOGI(TAG, "Skipping cleanup (no MQTT client)");
        return;
    }
    if (self->device_id == NULL) {
        /* No device_id — can't build subscription topic, mark done. */
        self->state = ha_cleanup_state_done;
        ESP_LOGW(TAG, "Skipping cleanup (no device_id)");
        return;
    }
    if (self->get_time_ms == NULL) {
        /* No time function — can't track drain timers, mark done. */
        self->state = ha_cleanup_state_done;
        ESP_LOGW(TAG, "Skipping cleanup (no get_time_ms)");
        return;
    }

    /* Not yet subscribed — subscribe to all domains at once. */
    if (!self->subscribed) {
        /* If we just unsubscribed, wait for the inbound MQTT event queue
         * to drain before re-subscribing. We know it's drained when no new
         * topic callbacks fire for DRAIN_WAIT_MS after the last one. */
        if (self->drain_start_ms != 0) {
            uint32_t now = self->get_time_ms();
            /* If a callback still fired after we started draining, the queue
             * isn't empty yet — reset the drain timer from the latest activity. */
            if (self->last_activity_ms > self->drain_start_ms) {
                self->drain_start_ms = self->last_activity_ms;
                return;
            }
            /* No new activity since drain started. Wait until enough time
             * has passed to be confident the queue is empty. */
            if (now - self->last_activity_ms < HA_CLEANUP_DRAIN_WAIT_MS) {
                return;
            }
            /* Queue has drained — clear drain state and proceed to subscribe. */
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

    /* Enforce minimum subscription time: don't consider the pass complete
     * until we've been subscribed long enough for the MQTT task to deliver
     * at least one batch of retained messages (~32 per queue cycle). */
    if (now - self->subscribe_start_ms < HA_CLEANUP_MIN_SUBSCRIBE_MS) {
        /* Still within minimum subscription window — flush one topic (fixes C4). */
        cleanup_flush_queue(self);
        return;
    }

    /* Must flush at least once after subscribing before declaring empty. */
    if (!self->flushed_once) {
        cleanup_flush_queue(self);
        self->flushed_once = true;
        return;
    }

    /* Check if we've been idle long enough (no new callbacks). */
    if (now - self->last_activity_ms >= HA_CLEANUP_IDLE_TIMEOUT_MS) {
        /* Flush one topic (fixes C4). */
        cleanup_flush_queue(self);

        char sub_topic[128];
        snprintf(sub_topic, sizeof(sub_topic),
            "homeassistant/+/%s/#", self->device_id);
        mqtt_client_unsubscribe(self->mqtt_client, sub_topic);
        self->subscribed = false;
        /* Start drain wait: track when we unsubscribed so we can wait
         * for the inbound queue to empty before re-subscribing. */
        self->drain_start_ms = self->last_activity_ms;

        if (self->pass_found_topics) {
            /* Found topics this pass — log and retry from scratch. */
            ESP_LOGI(TAG, "  Pass %u: %u received, %u removed, %u dropped — retrying",
                self->pass_number,
                self->pass_received_count,
                self->pass_removed_count,
                self->dropped_count);
            self->pass_number++;
            return;
        }

        /* Clean pass — no topics found. */
        ESP_LOGI(TAG, "  Pass %u: clean (%u received, %u removed)",
            self->pass_number,
            self->pass_received_count,
            self->pass_removed_count);
        self->clean_passes++;
        self->pass_number++;

        if (self->clean_passes < 2) {
            /* Need one more verification pass. */
            return;
        }

        /* Two clean verification passes — done. */
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

    /* Still receiving messages. Flush one topic while waiting (fixes C4). */
    cleanup_flush_queue(self);
}

void ha_discovery_cleanup_destroy(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return;

    /* Null get_time_ms first to poison the callback, preventing it from
     * firing on a partially-destroyed struct. */
    self->get_time_ms = NULL;

    /* Unsubscribe if we ever subscribed, regardless of whether device_id
     * is set. device_id == NULL only means "never configured", but a
     * double-destroy could have already zeroed it. */
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
    }
    memset(self, 0, sizeof(*self));
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

