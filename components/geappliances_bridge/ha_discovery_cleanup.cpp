
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

/* Uses a single wildcard subscription (homeassistant/+/{device_id}/#)
 * to catch all retained discovery topics across all domains at once. */

/* Idle timeout: must be long enough for the broker to finish delivering a
 * batch and for the MQTT task to process its queue (~32 messages per
 * inbound queue) before we flush. */

/* Minimum subscription time: ensures we wait for the initial retained
 * message burst even if cleanup_run() isn't called frequently. */

/* Wait after unsubscribe for the inbound MQTT event queue to drain before
 * re-subscribing; if no new callbacks fire during this window, the queue
 * is empty and it's safe to re-subscribe. */

/* Flush one topic per batch call: each publish allocates a std::string on
 * the heap, so one at a time minimizes peak heap pressure. */

#ifdef HA_DISCOVERY_CLEANUP_TEST_EXPORT
#  define CLEANUP_FN
#else
#  define CLEANUP_FN static
#endif

GEA_TAG(TAG) = "ha_cleanup";

/* Default allocator for the per-run topic queue. Plain malloc() matches the
 * discovery run buffers, so the queue and the publish run draw from the same
 * pool the largest-free-block gate in begin_publish_() measures. */
static void *cleanup_default_alloc(size_t size)
{
    return malloc(size);
}

static void cleanup_default_free(void *buffer)
{
    free(buffer);
}

/* Releases the per-run topic queue through the seam so a test can watch the
 * allocation balance. Idempotent: a restart without destroy() must not leak the
 * previous queue. Public because the owner is not always the cleanup module:
 * ha_discovery_manager_init() zeroes the whole manager struct, and a re-init
 * (Discovery Refresh, an aborted run) landing while a cleanup still holds its
 * queue would otherwise orphan it. Safe on a zero-initialized struct — the same
 * contract the manager's four run-buffer frees already rely on.
 *
 * The pointer is nulled under the same mux the callback's NULL check and
 * memcpy run in, so a callback that arrives after the NULL store bails, and
 * one already in its critical section finishes its memcpy before the free
 * (it holds the lock). The free itself happens after the lock is released:
 * the callback never sees a freed-but-non-NULL pointer, and no caller of this
 * function runs inside a critical section. */
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

/* Publishes empty retained payloads to remove topics. Called from
 * cleanup_run(), not the MQTT callback, to avoid blocking the ESP-IDF
 * framework MQTT task. Returns the number of topics remaining in the
 * queue (0 = all flushed). */
CLEANUP_FN uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self)
{
    if (self == NULL) return 0;
    if (self->mqtt_client == NULL) return self->queue_count;

    /* No queue allocated means nothing is queued, and the compaction below
     * would write through NULL. */
    if (self->topic_buf == NULL) return self->queue_count;

    /* A dropped removal publish holds off further attempts until the window
     * passes. The topic stays queued, so waiting loses nothing, while retrying
     * on every main-loop pass against a full outbound ring churns the backend's
     * drop counter and flaps the MQTT component's publish warning. The 0
     * sentinel is tested BEFORE the rollover-safe signed diff, which reads
     * now - 0 as negative once millis() passes 2^31 and would otherwise stall
     * every removal until the counter wraps. */
    if (self->retry_backoff_until_ms != 0 &&
        (int32_t)(self->get_time_ms() - self->retry_backoff_until_ms) < 0) {
        return self->queue_count;
    }

    /* Each topic is at most ~200 bytes
     * (homeassistant/{domain}/{device_id}/{entity_id}/config), so 256 is
     * safe; strncpy truncates. */
    char topic[256];
    uint16_t consumed;
    uint16_t remaining;

    taskENTER_CRITICAL(&self->mux);
    if (self->queue_count == 0) {
        taskEXIT_CRITICAL(&self->mux);
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

    /* Prevent underflow if the buffer is corrupted. */
    if (consumed > self->queue_write_pos) {
        consumed = self->queue_write_pos;
    }

    /* Publish BEFORE removing anything. The compaction below overwrites the
     * front topic's bytes, and restoring the counters afterwards cannot undo
     * that: the "undo" this replaces left the next topic at the front under the
     * old counts, so a dropped removal silently skipped a topic and left its
     * stale config in Home Assistant. The front of the buffer only ever moves
     * here -- the MQTT callback only appends -- so the topic copied above is
     * still at the front when the publish lands, even if callbacks appended
     * while it was in flight. */

    /* Leave the critical section before touching the network. On the
     * single-core parts taskENTER_CRITICAL disables interrupts, so a publish
     * that blocks on the outbound ring keeps them disabled long enough to trip
     * the interrupt watchdog, and any log the publish path emits aborts inside
     * newlib's stdio lock ("recursive mutexes make no sense in ISR context",
     * issue #198). Appends from the MQTT callback only ever move the buffer's
     * tail, so the front topic and `consumed` stay valid across the window. */
    taskEXIT_CRITICAL(&self->mux);
    if (!mqtt_client_publish_raw(self->mqtt_client, topic, "", 0, true)) {
        /* Post-attempt timestamp + backoff. queue_count is read unlocked on
         * purpose: a single aligned uint16_t load, and the return value is an
         * estimate for the caller's logging, not a decision. */
        self->retry_backoff_until_ms = self->get_time_ms()
            + HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS;
        ESP_LOGW(TAG, "Cleanup publish dropped, will retry: %s", topic);
        return self->queue_count;
    }

    self->retry_backoff_until_ms = 0;

    taskENTER_CRITICAL(&self->mux);
    /* Belt-and-braces against a future caller violating the single-threaded
     * invariant: only this function (main loop) shrinks the queue, and
     * ha_discovery_cleanup_destroy() is main-loop too, so the front this
     * publish used cannot have moved. If that ever stops holding, bail here
     * instead of letting queue_write_pos - consumed underflow and smear the
     * heap; the topic stays queued and the next flush republishes it, which is
     * harmless (an empty retained payload). */
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

    /* Counted only after the compaction succeeded: the bail path above leaves
     * the topic queued, and the retry that eventually removes it counts then. */
    self->total_removed_count++;

    ESP_LOGD(TAG, "Removed old topic: %s", topic);

    return remaining;
}

/* Callback for the homeassistant/+/{device_id}/# wildcard subscription.
 * Stores the full topic for republishing from the main loop; keeps the
 * callback short (no outbound publish) so the MQTT task's inbound queue
 * drains fast and retained message bursts don't overflow.
 *
 * Runs in the ESP-IDF framework MQTT task context; shared state
 * (topic_buf, queue_write_pos, queue_count) is protected by the
 * per-instance mux. On single-core (C3/C6) the critical section disables
 * interrupts; on dual-core (S3) the portMUX_TYPE is a spinlock, so
 * cross-core contention is handled regardless of which core the unpinned
 * MQTT task runs on.
 */
CLEANUP_FN void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg)
{
    (void)payload;
    ha_discovery_cleanup_t* self = (ha_discovery_cleanup_t*)arg;

    if (self == NULL) return;

    /* Read the time function exactly once and use that value for both the
     * guard and the later call: destroy() may null self->get_time_ms (and
     * later memset the struct) at any point. A second, separate read could
     * observe the post-poison NULL even when the guard's read was
     * non-NULL; only the optimizer's read-merging closes that window. */
    uint32_t (*get_time)(void) = self->get_time_ms;
    if (get_time == NULL) return;

    size_t topic_len = strlen(topic);
    if (topic_len < 7) return;
    if (strcmp(topic + topic_len - 7, "/config") != 0) return;

    /* An empty payload is our own echo from a previous clear — skip. */
    if (payload_len == 0) return;

    /* If topic_len >= HA_CLEANUP_TOPIC_BUF_SIZE, the uint16_t cast of
     * (topic_len + 1) could overflow to 0. */
    if (topic_len >= HA_CLEANUP_TOPIC_BUF_SIZE) {
        self->dropped_count++;
        return;
    }
    uint16_t needed = (uint16_t)(topic_len + 1);

    taskENTER_CRITICAL(&self->mux);
    /* Count all callbacks received (inside the critical section to avoid a race). */
    self->pass_received_count++;

    /* Linear append, no ring buffer. A NULL queue means the run never
     * allocated, or destroy() freed the queue while a delayed callback was
     * still in flight: count it the way a full queue is counted, so the pass
     * reports it and a later pass rediscovers the topic. */
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

    /* Fall back to the default when the struct was built without init() (the
     * test fixtures memset a struct and set only the fields they use). Such a
     * struct must still carry a valid mux: the store below enters the
     * spinlock, and a zeroed mux is owner=0 on hardware. make_cleanup() in the
     * test suite initializes it for exactly that reason. */
    void *(*alloc)(size_t) = self->alloc_fn != NULL ? self->alloc_fn : cleanup_default_alloc;
    char *buf = (char *) alloc(HA_CLEANUP_TOPIC_BUF_SIZE);
    if (buf == NULL) {
        /* Nothing has been removed yet, so refusing here is recoverable: the
         * caller leaves Home Assistant's retained configs alone and the next
         * boot tries again. */
        ESP_LOGE(TAG, "Cleanup refused: could not allocate the %u-byte topic queue; "
                      "the existing discovery configs were left in place",
                 (unsigned) HA_CLEANUP_TOPIC_BUF_SIZE);
        return false;
    }

    self->state = ha_cleanup_state_cleaning;
    cleanup_start(self);

    /* Publish the pointer under the same mux the callback's NULL check and
     * append run in, and only after the counters are reset: a dual-core
     * callback appending between the store and the reset would lose the
     * append uncounted. Symmetric with release_queue()'s NULL store. */
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
        /* Wait for the inbound MQTT event queue to drain before
         * re-subscribing: it's drained when no new topic callbacks fire
         * for DRAIN_WAIT_MS after the last one. */
        if (self->drain_start_ms != 0) {
            uint32_t now = self->get_time_ms();
            /* A callback fired after drain started — the queue isn't empty
             * yet; reset the drain timer from the latest activity. */
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

    /* Don't consider the pass complete until we've been subscribed long
     * enough for the MQTT task to deliver at least one batch of retained
     * messages (~32 per queue cycle). */
    if (now - self->subscribe_start_ms < HA_CLEANUP_MIN_SUBSCRIBE_MS) {
        cleanup_flush_queue(self);
        return;
    }

    /* Must flush at least once after subscribing before declaring empty. */
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
        /* Track when we unsubscribed so we can wait for the inbound queue
         * to empty before re-subscribing. */
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

        /* Two clean passes only prove no NEW topics arrived; topics still
         * queued from a dropped removal publish are outstanding work, and
         * declaring done there would leave stale discovery topics in Home
         * Assistant while the caller records the cleanup as having run. Read
         * the queue and declare done under the same lock the callback appends
         * under: an unlocked read could see 0 while a callback is appending on
         * the MQTT task. A topic a callback lands after this still sits queued
         * until the next run rediscovers it -- the same residual loss the
         * two-pass design already accepts, now with an accurate done report. */
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

    /* Null get_time_ms first to poison the callback, preventing it from
     * firing on a partially-destroyed struct. */
    self->get_time_ms = NULL;

    /* Unsubscribe if we ever subscribed, regardless of device_id: NULL
     * only means "never configured", but a double-destroy could have
     * already zeroed it. */
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

        /* Wait for any callback that read get_time_ms (non-NULL) before
         * the poison to finish its critical section. 50 ms is a
         * conservative margin (~1000x the actual CS duration). */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    /* Release the per-run topic queue only after the drain delay above: a
     * callback that read a non-NULL get_time_ms before the poison may still be
     * inside its critical section. */
    ha_discovery_cleanup_release_queue(self);

    memset(self, 0, sizeof(*self));

    /* Re-initialize the per-instance spinlock: the memset zeroed it to
     * owner=0, an invalid state. A delayed callback on the dead struct
     * would otherwise spin forever in spinlock_acquire (deadlock ->
     * watchdog reset) on dual-core. */
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

