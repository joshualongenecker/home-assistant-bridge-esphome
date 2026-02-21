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

static const char *const TAG = "geappliances_bridge.mqtt";

static std::string build_topic(esphome_mqtt_client_adapter_t* self, const char* suffix)
{
  return std::string("/geappliances/") + *self->device_id + suffix;
}

static void register_erd(i_mqtt_client_t* _self, tiny_erd_t erd)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  char topic_suffix[32];
  snprintf(topic_suffix, sizeof(topic_suffix), "/erd/0x%04X", erd);
  
  std::string value_topic = build_topic(self, (std::string(topic_suffix) + "/value").c_str());
  std::string write_topic = build_topic(self, (std::string(topic_suffix) + "/write").c_str());
  
  ESP_LOGD(TAG, "Registered ERD 0x%04X", erd);
  
  // Subscribe to write topic for this ERD
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->subscribe(
      write_topic,
      [self, erd](const std::string &topic, const std::string &payload) {
        // Parse hex string payload and trigger write request
        ESP_LOGD(TAG, "Write request for ERD 0x%04X: %s", erd, payload.c_str());
        
        // Convert hex string to bytes
        std::vector<uint8_t> data;
        for (size_t i = 0; i < payload.length(); i += 2) {
          if (i + 1 < payload.length()) {
            char byte_str[3] = {payload[i], payload[i+1], '\0'};
            data.push_back(static_cast<uint8_t>(strtol(byte_str, nullptr, 16)));
          }
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
  }
}

static void update_erd(i_mqtt_client_t* _self, tiny_erd_t erd, const void* value, uint8_t size)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  char topic_suffix[32];
  snprintf(topic_suffix, sizeof(topic_suffix), "/erd/0x%04X/value", erd);
  std::string topic = build_topic(self, topic_suffix);
  
  // Convert binary data to hex string
  std::string hex_payload;
  hex_payload.reserve(size * 2);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);
  for (uint8_t i = 0; i < size; i++) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", bytes[i]);
    hex_payload += hex;
  }
  
  // Publish to MQTT
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->publish(topic, hex_payload, 2, true);  // QoS 2, retain
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
  snprintf(topic_suffix, sizeof(topic_suffix), "/erd/0x%04X/write_result", erd);
  std::string topic = build_topic(self, topic_suffix);
  
  std::string payload = success ? "success" : "failure";
  if (!success) {
    char reason[16];
    snprintf(reason, sizeof(reason), " (reason: %d)", failure_reason);
    payload += reason;
  }
  
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->publish(topic, payload, 2, false);  // QoS 2, no retain
  }
  
  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, payload.c_str());
}

static void publish_sub_topic(i_mqtt_client_t* _self, const char* sub_topic, const char* payload)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  std::string suffix = std::string("/") + sub_topic;
  std::string topic = build_topic(self, suffix.c_str());
  
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->publish(topic, std::string(payload), 2, true);  // QoS 2, retain
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
  
  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
}

extern "C" void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self)
{
  // Publish the disconnect event to notify the bridge
  // This will clear the ERD registry and trigger resubscription
  tiny_event_publish(&self->on_mqtt_disconnect_event, nullptr);
}
