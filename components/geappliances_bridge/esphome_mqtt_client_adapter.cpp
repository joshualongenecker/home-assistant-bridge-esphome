#include "esphome_mqtt_client_adapter.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"

extern "C" {
#include "tiny_utils.h"
#include "tiny_event.h"
}

#include <cstdio>
#include <cstring>
#include <string>
#include <cctype>

static const char *const TAG __attribute__((unused)) = "geappliances_bridge.mqtt";

// Maximum number of pending ERD updates flushed to MQTT in a single
// notify_connected() / loop() drain call.  Keeping this small ensures
// the main loop() does not stall while the IDF MQTT client's API mutex is
// held by the MQTT task sending previous PUBLISH packets.
static constexpr size_t MAX_FLUSH_PER_CALL = 5;

static void build_topic(esphome_mqtt_client_adapter_t* self, char* out, size_t out_size, const char* suffix)
{
  // "geappliances/{device_id}{suffix}\0" — use memcpy for the fixed prefix
  // to avoid snprintf overhead on the hot path.
  static const char prefix[] = "geappliances/";
  size_t prefix_len = sizeof(prefix) - 1;
  size_t id_len = self->device_id->size();
  size_t suffix_len = strlen(suffix);
  size_t total = prefix_len + id_len + suffix_len;
  if (total >= out_size) {
    total = out_size - 1;
  }
  size_t pos = 0;
  memcpy(out, prefix, prefix_len);
  pos += prefix_len;
  memcpy(out + pos, self->device_id->c_str(), id_len);
  pos += id_len;
  memcpy(out + pos, suffix, suffix_len);
  pos += suffix_len;
  out[pos] = '\0';
}

// Find an existing dirty entry for this ERD, or return -1 if not found.
static int find_pending_entry(esphome_mqtt_client_adapter_t* self, tiny_erd_t erd)
{
  for (size_t i = 0; i < self->pending_count; i++) {
    if (self->pending_entries[i].dirty && self->pending_entries[i].erd == erd) {
      return (int)i;
    }
  }
  return -1;
}

// Compact the pending array by removing non-dirty entries.
static void compact_pending(esphome_mqtt_client_adapter_t* self)
{
  size_t write_idx = 0;
  for (size_t i = 0; i < self->pending_count; i++) {
    if (self->pending_entries[i].dirty) {
      if (write_idx != i) {
        self->pending_entries[write_idx] = self->pending_entries[i];
      }
      write_idx++;
    }
  }
  self->pending_count = write_idx;
}

static void register_erd(i_mqtt_client_t* _self, tiny_erd_t erd)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  // Track which ERDs the device registers so the bridge can filter
  // HA discovery entities to only those actually supported by the device.
  if (self->registered_erds_out != nullptr) {
    self->registered_erds_out->insert(erd);
  }

  ESP_LOGD(TAG, "Registered ERD 0x%04X", erd);

  // Write-command delivery is handled by a single wildcard MQTT subscription
  // (geappliances/{device_id}/erd/+/write) established in notify_connected().
  // No per-ERD subscribe() call is needed here, which avoids:
  //   - 108 heap-allocated lambda closures (one per ERD)
  //   - 108 IDF MQTT outbox entries (one SUBSCRIBE packet per ERD)
  //   - A 3-second stall on MQTT reconnect when ESPHome re-subscribes all
  //     108 topics synchronously, blocking loop() and triggering the TWDT
}

static void update_erd(i_mqtt_client_t* _self, tiny_erd_t erd, const void* value, uint8_t size)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  // If a valid ERD filter is set (appliance_api_parsing mode), skip ERDs not
  // in the validated list. This applies to both subscription and polling modes.
  if(self->valid_erds_filter != nullptr &&
     self->valid_erds_filter->find(erd) == self->valid_erds_filter->end()) {
    return;
  }

  // Validate inputs
  if (value == nullptr || size == 0) {
    ESP_LOGW(TAG, "Invalid ERD update: null value or zero size for ERD 0x%04X", erd);
    return;
  }

  // Build the topic suffix "/erd/0xXXXX/value" using a lookup table
  static const char hex_chars[] = "0123456789abcdef";
  char topic_suffix[20];
  topic_suffix[0] = '/'; topic_suffix[1] = 'e'; topic_suffix[2] = 'r';
  topic_suffix[3] = 'd'; topic_suffix[4] = '/'; topic_suffix[5] = '0';
  topic_suffix[6] = 'x';
  topic_suffix[7] = hex_chars[(erd >> 12) & 0x0f];
  topic_suffix[8] = hex_chars[(erd >> 8) & 0x0f];
  topic_suffix[9] = hex_chars[(erd >> 4) & 0x0f];
  topic_suffix[10] = hex_chars[erd & 0x0f];
  memcpy(topic_suffix + 11, "/value", 6);
  topic_suffix[17] = '\0';

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);

  // String-type ERDs: publish the raw bytes as a null-terminated ASCII string
  // instead of a hex string so Home Assistant displays human-readable text.
  bool is_string = (self->string_erds_filter != nullptr &&
                    self->string_erds_filter->find(erd) != self->string_erds_filter->end());

  // Build payload into a stack buffer
  char payload_buf[PENDING_PAYLOAD_SIZE];
  int payload_len = 0;
  if (is_string) {
    // Reserve only up to the first null byte (or full size if no null found)
    uint8_t str_len = 0;
    while (str_len < size && bytes[str_len] != 0) str_len++;
    for (uint8_t i = 0; i < str_len && payload_len < (int)(PENDING_PAYLOAD_SIZE - 1); i++) {
      if (isprint(bytes[i])) {
        payload_buf[payload_len++] = static_cast<char>(bytes[i]);
      } else {
        ESP_LOGD(TAG, "ERD 0x%04X: skipping non-printable byte 0x%02X at offset %u",
                 erd, bytes[i], i);
      }
    }
  } else {
    // Convert binary data to hex string using a lookup table (avoids snprintf)
    static const char hex_chars[] = "0123456789abcdef";
    for (uint8_t i = 0; i < size && payload_len + 2 < (int)PENDING_PAYLOAD_SIZE; i++) {
      payload_buf[payload_len++] = hex_chars[bytes[i] >> 4];
      payload_buf[payload_len++] = hex_chars[bytes[i] & 0x0f];
    }
  }
  payload_buf[payload_len] = '\0';

  // Find or create a pending entry for this ERD
  int idx = find_pending_entry(self, erd);
  if (idx >= 0) {
    // Overwrite existing entry in place
    build_topic(self, self->pending_entries[idx].topic, PENDING_TOPIC_SIZE, topic_suffix);
    memcpy(self->pending_entries[idx].payload, payload_buf, payload_len + 1);
  } else {
    // Append a new entry
    if (self->pending_count >= MAX_PENDING_ENTRIES) {
      ESP_LOGW(TAG, "Pending update queue full, dropping ERD update for 0x%04X", erd);
      return;
    }
    idx = (int)self->pending_count;
    self->pending_entries[idx].dirty = true;
    self->pending_entries[idx].erd = erd;
    build_topic(self, self->pending_entries[idx].topic, PENDING_TOPIC_SIZE, topic_suffix);
    memcpy(self->pending_entries[idx].payload, payload_buf, payload_len + 1);
    self->pending_count++;
  }
}

static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  static const char hex_chars[] = "0123456789abcdef";
  char topic_suffix[25];
  topic_suffix[0] = '/'; topic_suffix[1] = 'e'; topic_suffix[2] = 'r';
  topic_suffix[3] = 'd'; topic_suffix[4] = '/'; topic_suffix[5] = '0';
  topic_suffix[6] = 'x';
  topic_suffix[7] = hex_chars[(erd >> 12) & 0x0f];
  topic_suffix[8] = hex_chars[(erd >> 8) & 0x0f];
  topic_suffix[9] = hex_chars[(erd >> 4) & 0x0f];
  topic_suffix[10] = hex_chars[erd & 0x0f];
  memcpy(topic_suffix + 11, "/write_result", 13);
  topic_suffix[24] = '\0';

  char topic[PENDING_TOPIC_SIZE];
  build_topic(self, topic, PENDING_TOPIC_SIZE, topic_suffix);

  char payload[64];
  if (success) {
    memcpy(payload, "success", 7);
    payload[7] = '\0';
  } else {
    int len = snprintf(payload, sizeof(payload), "failure (reason: %d)", failure_reason);
    (void)len;
  }

  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, payload, 0, false);  // QoS 0, no retain
  } else {
    ESP_LOGD(TAG, "MQTT not connected, skipping write result for 0x%04X", erd);
  }

  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, payload);
}

static i_tiny_event_t* on_write_request(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  return &self->on_write_request_event.interface;
}

static i_tiny_event_t* on_mqtt_disconnect(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  return &self->on_mqtt_disconnect_event.interface;
}

static const i_mqtt_client_api_t api = {
  register_erd,
  update_erd,
  update_erd_write_result,
  on_write_request,
  on_mqtt_disconnect
};

extern "C" void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id)
{
  self->interface.api = &api;
  self->device_id = new std::string(device_id);
  memset(self->pending_entries, 0, sizeof(self->pending_entries));
  self->pending_count = 0;
  self->valid_erds_filter = nullptr;
  self->string_erds_filter = nullptr;
  self->registered_erds_out = nullptr;
  self->wildcard_subscribed   = false;
  self->mqtt_connected_at_ms  = 0;

  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
}

extern "C" void esphome_mqtt_client_adapter_set_valid_erds_filter(
  esphome_mqtt_client_adapter_t* self,
  const std::set<tiny_erd_t>* valid_erds_filter)
{
  self->valid_erds_filter = valid_erds_filter;
}

extern "C" void esphome_mqtt_client_adapter_set_string_erds_filter(
  esphome_mqtt_client_adapter_t* self,
  const std::set<tiny_erd_t>* string_erds_filter)
{
  self->string_erds_filter = string_erds_filter;
}

extern "C" void esphome_mqtt_client_adapter_set_registered_erds_out(
  esphome_mqtt_client_adapter_t* self,
  std::set<tiny_erd_t>* registered_erds_out)
{
  self->registered_erds_out = registered_erds_out;
}

extern "C" void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self)
{
  // Reset the connect timestamp so that notify_connected() re-records the
  // connect time on the next reconnect.
  self->mqtt_connected_at_ms = 0;
  // Publish the disconnect event to notify the bridge HSMs (subscription
  // bridge transitions back to state_subscribing; polling bridge restarts
  // its identify/polling cycle).
  tiny_event_publish(&self->on_mqtt_disconnect_event, nullptr);
}

extern "C" void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self)
{
  if (self->mqtt_connected_at_ms == 0) {
    self->mqtt_connected_at_ms = esphome::millis();
  }

  // Subscribe once to a single wildcard topic that covers write commands for
  // ALL ERDs.  This replaces 100+ individual per-ERD subscribe() calls with
  // one call.  Benefits:
  //   - On MQTT reconnect, ESPHome's MQTT client only re-subscribes 1 topic
  //     instead of 108, eliminating the 3-second block caused by 108
  //     synchronous subscribe() calls each acquiring the IDF MQTT API mutex.
  //   - Only 1 lambda closure on the heap instead of 108.
  //   - Only 1 SUBSCRIBE packet in the IDF MQTT outbox instead of 108.
  if (!self->wildcard_subscribed) {
    auto mqtt_client = esphome::mqtt::global_mqtt_client;
    if (mqtt_client != nullptr && mqtt_client->is_connected()) {
      char wildcard_topic[PENDING_TOPIC_SIZE];
      build_topic(self, wildcard_topic, PENDING_TOPIC_SIZE, "/erd/+/write");
      ESP_LOGI(TAG, "Subscribing to wildcard write topic: %s", wildcard_topic);

      mqtt_client->subscribe(
        std::string(wildcard_topic),
        [self](const std::string& topic, const std::string& payload) {
          // Parse the ERD number from the topic.
          // Topic format: geappliances/{device_id}/erd/0xXXXX/write
          size_t write_pos = topic.rfind("/write");
          if (write_pos == std::string::npos || write_pos < 2) {
            ESP_LOGW(TAG, "Ignoring write message with unexpected topic: %s", topic.c_str());
            return;
          }
          size_t erd_start = topic.rfind('/', write_pos - 1);
          if (erd_start == std::string::npos) {
            ESP_LOGW(TAG, "Could not parse ERD from topic: %s", topic.c_str());
            return;
          }
          erd_start++;  // skip the '/'
          std::string erd_str = topic.substr(erd_start, write_pos - erd_start);
          char* end;
          unsigned long val = strtoul(erd_str.c_str(), &end, 16);
          if (*end != '\0' || val > 0xFFFF) {
            ESP_LOGW(TAG, "Invalid ERD value in topic: %s", erd_str.c_str());
            return;
          }
          tiny_erd_t erd = static_cast<tiny_erd_t>(val);

          ESP_LOGD(TAG, "Write request for ERD 0x%04X: %s", erd, payload.c_str());

          if (payload.length() % 2 != 0) {
            ESP_LOGW(TAG, "Invalid hex payload for ERD 0x%04X: odd length (%zu)", erd, payload.length());
            return;
          }

          std::vector<uint8_t> data;
          data.reserve(payload.length() / 2);
          for (size_t i = 0; i < payload.length(); i += 2) {
            char byte_str[3] = {payload[i], payload[i + 1], '\0'};
            if (!std::isxdigit(static_cast<unsigned char>(payload[i])) ||
                !std::isxdigit(static_cast<unsigned char>(payload[i + 1]))) {
              ESP_LOGW(TAG, "Invalid hex characters in payload for ERD 0x%04X at position %zu", erd, i);
              return;
            }
            data.push_back(static_cast<uint8_t>(strtol(byte_str, nullptr, 16)));
          }

          if (data.empty() || data.size() > 255) {
            ESP_LOGW(TAG, "Invalid data size for ERD 0x%04X: %zu bytes", erd, data.size());
            return;
          }

          mqtt_client_on_write_request_args_t args = {
            .erd  = erd,
            .size = static_cast<uint8_t>(data.size()),
            .value = data.data()
          };
          tiny_event_publish(&self->on_write_request_event, &args);
        },
        0  // QoS 0
      );
      self->wildcard_subscribed = true;
    }
  }

  // Flush up to MAX_FLUSH_PER_CALL pending ERD updates per call.
  // loop() calls this every iteration while MQTT is connected so the full
  // backlog drains across multiple loop cycles without stalling the loop.
  if (self->pending_count == 0) {
    return;
  }
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr || !mqtt_client->is_connected()) {
    return;
  }
  size_t flushed = 0;
  for (size_t i = 0; i < self->pending_count && flushed < MAX_FLUSH_PER_CALL; i++) {
    if (!self->pending_entries[i].dirty) {
      continue;
    }
    mqtt_client->publish(self->pending_entries[i].topic, self->pending_entries[i].payload, 0, true);  // QoS 0, retain
    self->pending_entries[i].dirty = false;
    flushed++;
  }
  // Compact the array periodically to keep it small
  if (flushed > 0) {
    compact_pending(self);
    if (self->pending_count == 0) {
      ESP_LOGV(TAG, "Flushed all pending ERD updates");
    }
  }
}

extern "C" void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self)
{
  if (self->device_id != nullptr) {
    delete self->device_id;
    self->device_id = nullptr;
  }
  // pending_entries is inline, no heap cleanup needed
}

extern "C" size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self)
{
  size_t count = 0;
  for (size_t i = 0; i < self->pending_count; i++) {
    if (self->pending_entries[i].dirty) {
      count++;
    }
  }
  return count;
}
