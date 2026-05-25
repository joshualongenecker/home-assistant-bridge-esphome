#pragma once

#include <string>
#include <set>

extern "C" {
#include "i_mqtt_client.h"
#include "tiny_event.h"
}

// Flat pending update entry with inline buffers — no heap allocation per ERD
// update.  Topic is "geappliances/{device_id}/erd/0xXXXX/value" which is at
// most ~50 chars.  Payload is hex-encoded ERD data; max ERD value is 255 bytes
// = 510 hex chars, but in practice ERD values are <32 bytes so 64 chars is
// sufficient for the common case.  If the payload is longer it is silently
// truncated (the appliance won't send payloads that large for ERD values).
static constexpr size_t PENDING_TOPIC_SIZE = 64;
static constexpr size_t PENDING_PAYLOAD_SIZE = 64;

struct PendingErdEntry {
  bool dirty;
  tiny_erd_t erd;
  char topic[PENDING_TOPIC_SIZE];
  char payload[PENDING_PAYLOAD_SIZE];
};

static constexpr size_t MAX_PENDING_ENTRIES = 200;

typedef struct {
  i_mqtt_client_t interface;
  std::string* device_id;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;
  // Flat array of pending entries.  On update we search linearly for an
  // existing entry for this ERD (overwriting in place), or append a new one.
  // On drain we mark published entries as !dirty and compact.  This avoids
  // all heap allocation on the hot path — the topic/payload buffers are
  // inline in the struct.
  PendingErdEntry pending_entries[MAX_PENDING_ENTRIES];
  size_t pending_count;  // Number of dirty entries in pending_entries[]
  // Optional filter: when non-null, update_erd only publishes ERDs that are
  // present in this set. Used when appliance_api_parsing is enabled.
  const std::set<tiny_erd_t>* valid_erds_filter;
  // Optional set of string-type ERDs: when an ERD is in this set, update_erd
  // publishes the raw bytes as a null-terminated ASCII string instead of hex.
  const std::set<tiny_erd_t>* string_erds_filter;
  // Optional output set: when non-null, every ERD passed to register_erd() is
  // added here so the bridge can track which ERDs the device has registered.
  std::set<tiny_erd_t>* registered_erds_out;
  // True once the single wildcard MQTT subscription for write commands has been
  // established.  Set on the first MQTT connect after adapter init; never
  // cleared, because ESPHome's MQTT client automatically re-subscribes all
  // registered topics on reconnect, so we only need to call subscribe() once.
  bool wildcard_subscribed;
  // millis() timestamp of the most recent MQTT connection (set on the first
  // notify_connected() call after each disconnect; reset to 0 by
  // notify_disconnected()).  Used to gate the pending-update flush so the IDF
  // MQTT task has time to process the broker's reconnect backlog.
  uint32_t mqtt_connected_at_ms;
} esphome_mqtt_client_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id);

void esphome_mqtt_client_adapter_set_valid_erds_filter(
  esphome_mqtt_client_adapter_t* self,
  const std::set<tiny_erd_t>* valid_erds_filter);

void esphome_mqtt_client_adapter_set_string_erds_filter(
  esphome_mqtt_client_adapter_t* self,
  const std::set<tiny_erd_t>* string_erds_filter);

void esphome_mqtt_client_adapter_set_registered_erds_out(
  esphome_mqtt_client_adapter_t* self,
  std::set<tiny_erd_t>* registered_erds_out);

void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self);

size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self);

#ifdef __cplusplus
}
#endif
