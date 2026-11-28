#include "esphome_mqtt_client_adapter.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "erd_cache.h"
#include "erd_topic.h"

extern "C" {
#include "tiny_utils.h"
#include "tiny_event.h"
}

#include <cstdio>
#include <cstring>
#include <string>

GEA_TAG(TAG) = "geappliances_bridge.mqtt";
static const char* write_failure_reason_to_string(tiny_gea3_erd_client_write_failure_reason_t reason)
{
  switch (reason) {
    case tiny_gea3_erd_client_write_failure_reason_retries_exhausted: return "retries_exhausted";
    case tiny_gea3_erd_client_write_failure_reason_not_supported: return "not_supported";
    case tiny_gea3_erd_client_write_failure_reason_incorrect_size: return "incorrect_size";
    default: return "unknown";
  }
}

static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  uint8_t board_address,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  char topic[ERD_TOPIC_BUF_SIZE];
  if (erd_topic_format(topic, sizeof(topic), self->device_id, board_address, erd,
                       ERD_TOPIC_OP_WRITE_RESULT) < 0) {
    ESP_LOGW(TAG, "Write result topic truncated for ERD 0x%04X", erd);
    return;
  }

  char payload[32] = {0};
  if (success) {
    strcpy(payload, "ok");
  } else {
    const char* reason = write_failure_reason_to_string(failure_reason);
    int len = snprintf(payload, sizeof(payload), "{\"error\":\"%s\"}", reason);
    if (len < 0 || (size_t)len >= sizeof(payload)) {
      ESP_LOGW(TAG, "Write result payload truncated for ERD 0x%04X", erd);
    }
  }

  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, payload);
  esphome_mqtt_client_adapter_publish_raw(_self, topic, payload, strlen(payload), true);
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

static i_tiny_event_t* on_mqtt_connect(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  return &self->on_mqtt_connect_event.interface;
}

static bool adapter_is_connected(i_mqtt_client_t* _self)
{
  (void)_self;
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  return mqtt_client != nullptr && mqtt_client->is_connected();
}

static const i_mqtt_client_api_t api = {
  update_erd_write_result,
  on_write_request,
  on_mqtt_disconnect,
  on_mqtt_connect,
  adapter_is_connected,
  esphome_mqtt_client_adapter_publish_raw,
  esphome_mqtt_client_adapter_subscribe,
  esphome_mqtt_client_adapter_unsubscribe
};

extern "C" void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id)
{
  self->initialized = true;
  self->interface.api = &api;
  self->device_id = device_id;

  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
  tiny_event_init(&self->on_mqtt_connect_event);

  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->set_on_connect([self](bool) {
      esphome_mqtt_client_adapter_notify_connected(self);
    });
    mqtt_client->set_on_disconnect([self](esphome::mqtt::MQTTClientDisconnectReason) {
      esphome_mqtt_client_adapter_notify_disconnected(self);
    });

    if (mqtt_client->is_connected()) {
      esphome_mqtt_client_adapter_notify_connected(self);
    }
  }
}

extern "C" void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self)
{
  tiny_event_publish(&self->on_mqtt_disconnect_event, nullptr);
}

extern "C" void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self)
{
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr) return;

  if (erd_topic_format_write_filter(self->write_topic_, sizeof(self->write_topic_),
                                    self->device_id) < 0) {
    ESP_LOGW(TAG, "Write topic filter too long for device_id: %s", self->device_id);
    return;
  }

  mqtt_client->subscribe(self->write_topic_, [self](const std::string& topic, const std::string& payload) {
    uint8_t board_address = 0;
    uint16_t erd = 0;
    erd_topic_op_t op = ERD_TOPIC_OP_VALUE;
    if (!erd_topic_parse(topic.c_str(), self->device_id, &board_address, &erd, &op)) {
      ESP_LOGW(TAG, "Ignoring write on non-grammar topic \"%s\": the 0x prefix and the "
                    "operation name must be lowercase (docs/reference/mqtt-topics.md)",
               topic.c_str());
      return;
    }
    if (op != ERD_TOPIC_OP_WRITE) return;

    uint8_t local_buffer[ERD_CACHE_MAX_DATA_SIZE];
    size_t decoded = 0;
    for (size_t i = 0; i + 1 < payload.size() && decoded < sizeof(local_buffer); i += 2) {
      unsigned byte = 0;
      if (sscanf(&payload[i], "%2x", &byte) == 1) {
        local_buffer[decoded++] = static_cast<uint8_t>(byte);
      } else {
        break;
      }
    }
    uint8_t local_size = static_cast<uint8_t>(decoded);

    mqtt_client_on_write_request_args_t args;
    args.erd = erd;
    args.board_address = board_address;
    args.size = local_size;
    args.value = local_buffer;

    tiny_event_publish(&self->on_write_request_event, &args);
  }, 0);
}
extern "C" void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self)
{
  tiny_event_publish(&self->on_mqtt_connect_event, nullptr);
}

extern "C" void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self)
{
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->set_on_connect(nullptr);
    mqtt_client->set_on_disconnect(nullptr);
    if (self->write_topic_[0] != '\0') {
      mqtt_client->unsubscribe(self->write_topic_);
    }
  }

  self->write_topic_[0] = '\0';
  self->device_id = nullptr;
  self->initialized = false;
}
extern "C" bool esphome_mqtt_client_adapter_publish_raw(
  i_mqtt_client_t* _self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain)
{
  (void)_self;
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    return mqtt_client->publish(topic, payload, payload_len, 0, retain);
  }
  return false;
}

extern "C" void esphome_mqtt_client_adapter_subscribe(
  i_mqtt_client_t* _self,
  const char* topic,
  void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg),
  void* arg)
{
  (void)_self;
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr) return;

  mqtt_client->subscribe(topic, [callback, arg](const std::string& t, const std::string& p) {
    callback(t.c_str(), p.c_str(), p.size(), arg);
  }, 0);
}

extern "C" void esphome_mqtt_client_adapter_unsubscribe(
  i_mqtt_client_t* _self,
  const char* topic)
{
  (void)_self;
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->unsubscribe(topic);
  }
}
