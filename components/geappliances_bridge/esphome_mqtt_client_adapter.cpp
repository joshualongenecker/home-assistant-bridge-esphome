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

  // Use a stack buffer to avoid heap allocation.
  // Max error payload: "{\"error\":\"retries_exhausted\"}" = 28 chars + null.
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
  /* Publish through the adapter's own publish_raw slot so the ESPHome global
   * client is reached through one seam, not reached around. */
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

static const i_mqtt_client_api_t api = {
  update_erd_write_result,
  on_write_request,
  on_mqtt_disconnect,
  on_mqtt_connect,
  esphome_mqtt_client_adapter_publish_raw,
  esphome_mqtt_client_adapter_subscribe,
  esphome_mqtt_client_adapter_unsubscribe
};

extern "C" void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id)
{
  self->interface.api = &api;
  /* Lifetime: stores a pointer into DeviceIdentityManager's buffers
   * (configured_device_id_ or generated_device_id_), which live in the
   * bridge object and outlive the adapter. */
  self->device_id = device_id;

  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
  tiny_event_init(&self->on_mqtt_connect_event);

  // Wire ESPHome MQTT client connect/disconnect callbacks to our tiny events.
  // Without this, mqtt_connected stays false forever and the publisher never publishes.
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->set_on_connect([self](bool) {
      esphome_mqtt_client_adapter_notify_connected(self);
    });
    mqtt_client->set_on_disconnect([self](esphome::mqtt::MQTTClientDisconnectReason) {
      esphome_mqtt_client_adapter_notify_disconnected(self);
    });

    // If already connected when we register, fire the event immediately so the
    // publisher's mqtt_connected flag is set correctly on first loop().
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
    // Grammar and sentinel rules live in erd_topic.h: no-prefix topics
    // parse to the ERD_TOPIC_PRIMARY (0xFF) sentinel, which the write
    // bridge resolves to the detected host address; a secondary board at
    // 0x00 parses as 0x00 and stays distinct from the sentinel. Topics
    // that do not match the grammar exactly are dropped.
    uint8_t board_address = 0;
    uint16_t erd = 0;
    erd_topic_op_t op = ERD_TOPIC_OP_VALUE;
    if (!erd_topic_parse(topic.c_str(), self->device_id, &board_address, &erd, &op)) return;
    if (op != ERD_TOPIC_OP_WRITE) return;

    // Decode hex payload to a local stack buffer to avoid race condition:
    // if a new MQTT message arrives before tiny_event_publish() delivers
    // this one, the local buffer is already consumed by the event handler.
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

    // tiny_event_publish() is synchronous (subscriber callback runs to
    // completion before return), so the stack buffer is valid for the
    // duration of the event delivery.
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
  // Unregister all MQTT callbacks to prevent dangling lambda captures
  // from firing after the adapter is gone (e.g., on re-init or OTA).
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->set_on_connect(nullptr);
    mqtt_client->set_on_disconnect(nullptr);
    // Unsubscribe from write topic (also captures 'self' by raw pointer).
    if (self->write_topic_[0] != '\0') {
      mqtt_client->unsubscribe(self->write_topic_);
    }
  }

  self->write_topic_[0] = '\0';
  self->device_id = nullptr;
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
