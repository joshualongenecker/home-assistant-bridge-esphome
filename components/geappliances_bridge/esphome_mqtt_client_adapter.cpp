#include "esphome_mqtt_client_adapter.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"

extern "C" {
#include "tiny_utils.h"
#include "tiny_event.h"
}

#include <cstdio>
#include <string>
#include <cctype>
#include <map>

static const char *const TAG __attribute__((unused)) = "geappliances_bridge.mqtt";

// Maximum number of distinct ERDs that can be pending (safety bound — in
// practice bounded by the number of ERDs the appliance registers, typically <100).
static constexpr size_t MAX_PENDING_UPDATES = 200;

// Maximum number of pending ERD updates flushed to MQTT in a single
// notify_connected() / loop() drain call.  Keeping this small (≤5) ensures
// the main loop() does not stall while the IDF MQTT client's API mutex is
// held by the MQTT task sending previous PUBLISH packets.  At 5 per call and
// a typical loop rate of ~200 Hz, 200 pending updates drain in ≤200 ms.
static constexpr size_t MAX_FLUSH_PER_CALL = 5;

static std::string build_topic(esphome_mqtt_client_adapter_t* self, const char* suffix)
{
  return std::string("geappliances/") + *self->device_id + suffix;
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
  
  char topic_suffix[32];
  snprintf(topic_suffix, sizeof(topic_suffix), "/erd/0x%04x/value", erd);
  std::string topic = build_topic(self, topic_suffix);

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);

  // String-type ERDs: publish the raw bytes as a null-terminated ASCII string
  // instead of a hex string so Home Assistant displays human-readable text.
  bool is_string = (self->string_erds_filter != nullptr &&
                    self->string_erds_filter->find(erd) != self->string_erds_filter->end());

  std::string payload;
  if (is_string) {
    // Reserve only up to the first null byte (or full size if no null found)
    uint8_t str_len = 0;
    while (str_len < size && bytes[str_len] != 0) str_len++;
    payload.reserve(str_len);
    for (uint8_t i = 0; i < str_len; i++) {
      if (isprint(bytes[i])) {
        payload += static_cast<char>(bytes[i]);
      } else {
        ESP_LOGD(TAG, "ERD 0x%04X: skipping non-printable byte 0x%02X at offset %u",
                 erd, bytes[i], i);
      }
    }
  } else {
    // Convert binary data to hex string
    payload.reserve(size * 2);
    for (uint8_t i = 0; i < size; i++) {
      char hex[3];
      snprintf(hex, sizeof(hex), "%02x", bytes[i]);
      payload += hex;
    }
  }
  
  // Always queue the update in the pending map rather than publishing
  // directly.  The map key is the ERD so a repeated update overwrites the
  // previous pending value — only the most recent value is ever published.
  //
  // This keeps the main loop non-blocking even when the IDF MQTT outbox is
  // full (e.g., slow network or busy broker).  The notify_connected() drain
  // (called every loop() iteration while MQTT is connected) publishes up to
  // MAX_FLUSH_PER_CALL per call, spreading the burst across multiple loop
  // iterations without stalling the loop.
  if (self->pending_updates != nullptr && self->pending_updates->size() < MAX_PENDING_UPDATES) {
    (*self->pending_updates)[erd] = {topic, payload};
  } else if (self->pending_updates == nullptr) {
    ESP_LOGW(TAG, "Pending updates queue not initialized, dropping ERD update for 0x%04X", erd);
  } else {
    ESP_LOGW(TAG, "Pending update queue full, dropping ERD update for 0x%04X", erd);
  }
}

static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  char topic_suffix[48];
  snprintf(topic_suffix, sizeof(topic_suffix), "/erd/0x%04x/write_result", erd);
  std::string topic = build_topic(self, topic_suffix);
  
  std::string payload = success ? "success" : "failure";
  if (!success) {
    char reason[16];
    snprintf(reason, sizeof(reason), " (reason: %d)", failure_reason);
    payload += reason;
  }
  
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, payload, 0, false);  // QoS 0, no retain
  } else {
    ESP_LOGD(TAG, "MQTT not connected, skipping write result for 0x%04X", erd);
  }
  
  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, payload.c_str());
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
  self->pending_updates = new std::map<tiny_erd_t, PendingErdUpdate>();
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
      std::string wildcard_topic = build_topic(self, "/erd/+/write");
      ESP_LOGI(TAG, "Subscribing to wildcard write topic: %s", wildcard_topic.c_str());

      mqtt_client->subscribe(
        wildcard_topic,
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
            ESP_LOGW(TAG, "Invalid ERD value in topic: %s", topic.c_str());
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
  if (self->pending_updates == nullptr || self->pending_updates->empty()) {
    return;
  }
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr || !mqtt_client->is_connected()) {
    return;
  }
  size_t flushed = 0;
  while (!self->pending_updates->empty() && flushed < MAX_FLUSH_PER_CALL) {
    auto it = self->pending_updates->begin();
    mqtt_client->publish(it->second.topic, it->second.payload, 0, true);  // QoS 0, retain
    self->pending_updates->erase(it);
    flushed++;
  }
  if (flushed > 0 && self->pending_updates->empty()) {
    ESP_LOGV(TAG, "Flushed all pending ERD updates");
  }
}

extern "C" void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self)
{
  if (self->device_id != nullptr) {
    delete self->device_id;
    self->device_id = nullptr;
  }
  if (self->pending_updates != nullptr) {
    delete self->pending_updates;
    self->pending_updates = nullptr;
  }
}

extern "C" size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self)
{
  if (self->pending_updates == nullptr) {
    return 0;
  }
  return self->pending_updates->size();
}
