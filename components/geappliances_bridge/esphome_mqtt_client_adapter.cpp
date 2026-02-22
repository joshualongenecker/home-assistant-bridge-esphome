#include "esphome_mqtt_client_adapter.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

extern "C" {
#include "tiny_utils.h"
#include "tiny_event.h"
}

#include <cstdio>
#include <string>
#include <cctype>
#include <deque>

static const char *const TAG = "geappliances_bridge.mqtt";

// Maximum number of pending updates to queue (prevent memory exhaustion)
static constexpr size_t MAX_PENDING_UPDATES = 100;

// Maximum number of pending ERD registrations to queue
static constexpr size_t MAX_PENDING_REGISTRATIONS = 100;

// Maximum length for ERD topic suffix (e.g., "/erd/0x7135/write" = 19 chars + null)
static constexpr size_t MAX_ERD_TOPIC_SUFFIX_LENGTH = 48;

static std::string build_topic(esphome_mqtt_client_adapter_t* self, const char* suffix)
{
  return std::string("geappliances/") + *self->device_id + suffix;
}

static void register_erd(i_mqtt_client_t* _self, tiny_erd_t erd)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  // Queue the ERD registration for later processing to avoid blocking
  if (self->pending_registrations != nullptr && 
      self->pending_registrations->size() < MAX_PENDING_REGISTRATIONS) {
    self->pending_registrations->push_back(erd);
    ESP_LOGD(TAG, "Queued ERD 0x%04X for registration (queue size: %zu)", 
             erd, self->pending_registrations->size());
  } else if (self->pending_registrations == nullptr) {
    ESP_LOGW(TAG, "Pending registrations queue not initialized, dropping ERD registration for 0x%04X", erd);
  } else {
    ESP_LOGW(TAG, "Pending registration queue full, dropping ERD registration for 0x%04X", erd);
  }
}

static void update_erd(i_mqtt_client_t* _self, tiny_erd_t erd, const void* value, uint8_t size)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  // Validate inputs
  if (value == nullptr || size == 0) {
    ESP_LOGW(TAG, "Invalid ERD update: null value or zero size for ERD 0x%04X", erd);
    return;
  }
  
  char topic_suffix[32];
  snprintf(topic_suffix, sizeof(topic_suffix), "/erd/0x%04x/value", erd);
  std::string topic = build_topic(self, topic_suffix);
  
  // Convert binary data to hex string
  std::string hex_payload;
  hex_payload.reserve(size * 2);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);
  for (uint8_t i = 0; i < size; i++) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", bytes[i]);
    hex_payload += hex;
  }
  
  // Publish to MQTT or queue if not connected
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, hex_payload, 2, true);  // QoS 2, retain
  } else {
    // Queue the update for later when MQTT connects
    if (self->pending_updates != nullptr && self->pending_updates->size() < MAX_PENDING_UPDATES) {
      self->pending_updates->push_back({topic, hex_payload});
      ESP_LOGD(TAG, "MQTT not connected, queued ERD update for 0x%04X (queue size: %zu)", 
               erd, self->pending_updates->size());
    } else if (self->pending_updates == nullptr) {
      ESP_LOGW(TAG, "Pending updates queue not initialized, dropping ERD update for 0x%04X", erd);
    } else {
      ESP_LOGW(TAG, "Pending update queue full, dropping ERD update for 0x%04X", erd);
    }
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
    mqtt_client->publish(topic, payload, 2, false);  // QoS 2, no retain
  } else {
    ESP_LOGD(TAG, "MQTT not connected, skipping write result for 0x%04X", erd);
  }
  
  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, payload.c_str());
}

static void publish_sub_topic(i_mqtt_client_t* _self, const char* sub_topic, const char* payload)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  std::string suffix = std::string("/") + sub_topic;
  std::string topic = build_topic(self, suffix.c_str());
  
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, std::string(payload), 2, true);  // QoS 2, retain
  } else {
    ESP_LOGD(TAG, "MQTT not connected, skipping sub-topic publish for %s", sub_topic);
  }
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
  publish_sub_topic,
  on_write_request,
  on_mqtt_disconnect
};

extern "C" void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id)
{
  self->interface.api = &api;
  self->device_id = new std::string(device_id);
  self->pending_updates = new std::deque<PendingErdUpdate>();
  self->pending_registrations = new std::deque<tiny_erd_t>();
  
  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
}

extern "C" void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self)
{
  // Clear any pending registrations since they will need to be re-queued
  if (self->pending_registrations != nullptr) {
    self->pending_registrations->clear();
  }
  
  // Publish the disconnect event to notify the bridge
  // This will clear the ERD registry and trigger resubscription
  tiny_event_publish(&self->on_mqtt_disconnect_event, nullptr);
}

extern "C" void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self)
{
  // Flush pending updates when MQTT connects
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected() && 
      self->pending_updates != nullptr && !self->pending_updates->empty()) {
    ESP_LOGI(TAG, "MQTT connected, flushing %zu pending ERD updates", self->pending_updates->size());
    
    for (const auto& update : *self->pending_updates) {
      mqtt_client->publish(update.topic, update.payload, 2, true);  // QoS 2, retain
    }
    
    self->pending_updates->clear();
    ESP_LOGI(TAG, "Flushed all pending ERD updates");
  }
}

extern "C" void esphome_mqtt_client_adapter_process_registrations(
  esphome_mqtt_client_adapter_t* self)
{
  // Process one ERD registration at a time to avoid blocking
  if (self->pending_registrations == nullptr || self->pending_registrations->empty()) {
    return;
  }
  
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr || !mqtt_client->is_connected()) {
    // Don't process registrations if MQTT is not connected
    return;
  }
  
  // Peek at the next ERD without removing it yet
  tiny_erd_t erd = self->pending_registrations->front();
  
  // Build write topic suffix
  char write_suffix[MAX_ERD_TOPIC_SUFFIX_LENGTH];
  snprintf(write_suffix, sizeof(write_suffix), "/erd/0x%04x/write", erd);
  
  std::string write_topic = build_topic(self, write_suffix);
  
  // Subscribe to write topic for this ERD
  // Note: ESPHome's subscribe() doesn't return a status, so we assume success
  mqtt_client->subscribe(
    write_topic,
    [self, erd](const std::string &topic, const std::string &payload) {
      // Parse hex string payload and trigger write request
      ESP_LOGD(TAG, "Write request for ERD 0x%04X: %s", erd, payload.c_str());
      
      // Validate hex string format
      if (payload.length() % 2 != 0) {
        ESP_LOGW(TAG, "Invalid hex payload for ERD 0x%04X: odd length (%zu)", erd, payload.length());
        return;
      }
      
      // Convert hex string to bytes
      std::vector<uint8_t> data;
      data.reserve(payload.length() / 2);
      for (size_t i = 0; i < payload.length(); i += 2) {
        char byte_str[3] = {payload[i], payload[i+1], '\0'};
        // Validate hex characters
        if (!std::isxdigit(static_cast<unsigned char>(payload[i])) || 
            !std::isxdigit(static_cast<unsigned char>(payload[i+1]))) {
          ESP_LOGW(TAG, "Invalid hex characters in payload for ERD 0x%04X at position %zu", erd, i);
          return;
        }
        data.push_back(static_cast<uint8_t>(strtol(byte_str, nullptr, 16)));
      }
      
      // Validate data size
      if (data.size() == 0 || data.size() > 255) {
        ESP_LOGW(TAG, "Invalid data size for ERD 0x%04X: %zu bytes", erd, data.size());
        return;
      }
      
      // Publish write request event
      mqtt_client_on_write_request_args_t args = {
        .erd = erd,
        .size = static_cast<uint8_t>(data.size()),
        .value = data.data()
      };
      tiny_event_publish(&self->on_write_request_event, &args);
    },
    2  // QoS 2
  );
  
  // Remove from queue after subscription call completes
  // Note: ESPHome's subscribe() returns void, so we cannot verify success and must
  // remove from queue to avoid infinite retries. On reconnect, ERDs will be re-queued.
  self->pending_registrations->pop_front();
  
  ESP_LOGD(TAG, "Registered ERD 0x%04X", erd);
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
  if (self->pending_registrations != nullptr) {
    delete self->pending_registrations;
    self->pending_registrations = nullptr;
  }
}
