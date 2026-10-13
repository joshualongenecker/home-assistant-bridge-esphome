
#include "erd_write_bridge.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include "tiny_gea_constants.h"
#include "erd_bridge_common.h"

enum {
  signal_write_requested = tiny_hsm_signal_user_start,
  signal_write_completed,
  signal_write_failed
};

GEA_TAG(TAG) = "erd_write_bridge";

static tiny_hsm_result_t write_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_ready(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_writing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

/* Look up the generated RMW table for a request ERD. Returns the row (with
 * the base ERD and payload size) or nullptr if the ERD is not an RMW target. */
static const rmw_erd_t* rmw_find(tiny_erd_t request_erd)
{
  for (uint16_t i = 0; i < rmw_erd_count; i++) {
    if (rmw_erds[i].request_erd == request_erd) {
      return &rmw_erds[i];
    }
  }
  return nullptr;
}

/* Patch a single field into a full-payload buffer. Byte-aligned fields
 * (bit_offset 0, bit_size == byte_size*8) are a plain memcpy; otherwise each
 * target bit is set/cleared from the value's corresponding bit. The rendered
 * value is big-endian (MSB first), so field bit i (0 = LSB) is
 * (value[value_size - 1 - i/8] >> (i%8)) & 1; see the bit branch below for
 * the matching big-endian payload placement.
 *
 * Returns false (and patches nothing) when the command value is not exactly
 * byte_size bytes for a byte-aligned field: too short would read
 * uninitialized stack bytes, and too long would silently MSB-truncate (copy
 * the high-order bytes and discard the low-order ones), writing a wrong
 * value. The bit-field branch is always safe (missing value bits read as 0).
 */
static bool rmw_patch_field(uint8_t* buffer, uint16_t byte_offset, uint8_t byte_size,
                            uint8_t bit_offset, uint8_t bit_size,
                            const uint8_t* value, uint8_t value_size)
{
  if (bit_offset == 0 && bit_size == byte_size * 8) {
    if (value_size != byte_size) {
      return false;
    }
    memcpy(buffer + byte_offset, value, byte_size);
    return true;
  }
  // The command renders the field value big-endian (MSB first), and the
  // appliance/VT reads the field as a big-endian integer of byte_size bytes.
  // Field bit i (0 = LSB) is therefore bit (bit_offset + i) of that integer:
  //   - in the rendered value: byte (value_size - 1 - i/8), bit (i%8);
  //   - in the payload: byte (byte_offset + byte_size - 1 - (bit_offset+i)/8),
  //     bit ((bit_offset+i)%8).
  // For byte_size == 1 (all current RMW rows) this reduces to the naive
  // little-endian placement, so existing behavior is unchanged.
  for (uint8_t i = 0; i < bit_size; i++) {
    uint8_t vbit = (i / 8 < value_size)
        ? ((value[value_size - 1 - i / 8] >> (i % 8)) & 1) : 0;
    uint32_t b = (uint32_t)bit_offset + i;
    uint32_t abs_bit = ((uint32_t)byte_offset + (uint32_t)byte_size - 1 - b / 8) * 8 + (b % 8);
    uint8_t* byte = &buffer[abs_bit / 8];
    uint8_t mask = (uint8_t)(1u << (abs_bit % 8));
    if (vbit) {
      *byte |= mask;
    } else {
      *byte &= (uint8_t)~mask;
    }
  }
  return true;
}

static tiny_hsm_result_t write_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)hsm;
  (void)signal;
  (void)data;
  return tiny_hsm_result_signal_deferred;
}

static tiny_hsm_result_t state_ready(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_write_bridge_t* self = container_of(erd_write_bridge_t, hsm, hsm);

  switch(signal) {
    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);

      if(self->erd_host_address == tiny_gea_broadcast_address) {
        ESP_LOGW(TAG, "Write request for ERD 0x%04x dropped: appliance not identified", args->erd);
        mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
          tiny_gea3_erd_client_write_failure_reason_not_supported);
        break;
      }

      // Primary-board topics (board_address == 0xFF, the broadcast sentinel)
      // route to the detected host address; secondary-board topics route to
      // the parsed address.
      uint8_t target_address = (args->board_address != tiny_gea_broadcast_address) ? args->board_address : self->erd_host_address;

      // The payload and size to write. For a full write this is the command
      // value verbatim; for a field write it is the full RMW payload (the
      // snapshotted base with the target field patched).
      const void* write_value = args->value;
      uint8_t write_size = args->size;
      // Declared outside the field-write block: write_value points into it and
      // is used by tiny_gea3_erd_client_write() after the block, so it must
      // stay in scope (a 248-byte stack cost on the full-write path is a minor
      // inefficiency, not a correctness issue).
      uint8_t rmw_buffer[ERD_CACHE_MAX_DATA_SIZE];

      if (args->is_field_write) {
        const rmw_erd_t* rmw = rmw_find(args->erd);
        if (rmw == nullptr) {
          ESP_LOGW(TAG, "Field write for ERD 0x%04x dropped: not an RMW target", args->erd);
          mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
            tiny_gea3_erd_client_write_failure_reason_not_supported);
          break;
        }
        // The field must fit within the base's actual payload. The adapter only
        // bounds the geometry to the 248-byte ERD max; a rogue topic whose field
        // extends past this base's payload_size would patch bytes that are never
        // sent (write_size is payload_size), silently truncating the write.
        if (args->byte_offset + args->byte_size > rmw->payload_size) {
          ESP_LOGW(TAG, "Field write for ERD 0x%04x dropped: field (off %u size %u) exceeds payload %u",
            args->erd, args->byte_offset, args->byte_size, rmw->payload_size);
          mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
            tiny_gea3_erd_client_write_failure_reason_incorrect_size);
          break;
        }
        if (self->cache == nullptr) {
          ESP_LOGW(TAG, "Field write for ERD 0x%04x dropped: no ERD cache", args->erd);
          mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
            tiny_gea3_erd_client_write_failure_reason_not_supported);
          break;
        }
        // Snapshot the base (the paired status ERD) out of the cache. The lock
        // is held only for the short copy, never across the slow write below.
        uint8_t base_size = 0;
        if (!erd_cache_snapshot_entry(self->cache, rmw->base_erd, target_address, rmw_buffer, &base_size)
            || base_size != rmw->payload_size) {
          ESP_LOGW(TAG, "Field write for ERD 0x%04x dropped: base 0x%04x not cached (size %u, expected %u)",
            args->erd, rmw->base_erd, base_size, rmw->payload_size);
          mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
            tiny_gea3_erd_client_write_failure_reason_incorrect_size);
          break;
        }
        if (!rmw_patch_field(rmw_buffer, args->byte_offset, args->byte_size, args->bit_offset, args->bit_size,
                             (const uint8_t*)args->value, args->size)) {
          ESP_LOGW(TAG, "Field write for ERD 0x%04x dropped: value too short for field (size %u)",
            args->erd, args->size);
          mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
            tiny_gea3_erd_client_write_failure_reason_incorrect_size);
          break;
        }
        write_value = rmw_buffer;
        write_size = rmw->payload_size;
      }

      tiny_gea3_erd_client_request_id_t request_id;
      if(!tiny_gea3_erd_client_write(self->erd_client, &request_id, target_address,
         args->erd, write_value, write_size)) {
        ESP_LOGW(TAG, "Write request for ERD 0x%04x failed to queue", args->erd);
        mqtt_client_update_erd_write_result(self->mqtt_client, args->erd, args->board_address, false,
          tiny_gea3_erd_client_write_failure_reason_retries_exhausted);
        break;
      }

      self->pending_request_id = request_id;
      self->pending_erd = args->erd;
      self->pending_board_address = args->board_address;
      tiny_hsm_transition(hsm, state_writing);
    } break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_writing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_write_bridge_t* self = container_of(erd_write_bridge_t, hsm, hsm);

  switch(signal) {
    case signal_write_requested: {
      [[maybe_unused]] auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);
      ESP_LOGW(TAG, "Write request for ERD 0x%04x dropped: write already in progress", args->erd);
    } break;

    case signal_write_completed: {
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      if (args->write_completed.request_id != self->pending_request_id) {
        ESP_LOGW(TAG, "Stale write completion for request_id %u (expected %u); ignoring",
          args->write_completed.request_id, self->pending_request_id);
        break;
      }
      mqtt_client_update_erd_write_result(self->mqtt_client, self->pending_erd, self->pending_board_address, true,
        0);
      tiny_hsm_transition(hsm, state_ready);
    } break;

    case signal_write_failed: {
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      if (args->write_failed.request_id != self->pending_request_id) {
        ESP_LOGW(TAG, "Stale write failure for request_id %u (expected %u); ignoring",
          args->write_failed.request_id, self->pending_request_id);
        break;
      }
      mqtt_client_update_erd_write_result(self->mqtt_client, self->pending_erd, self->pending_board_address, false,
        args->write_failed.reason);
      tiny_hsm_transition(hsm, state_ready);
    } break;
    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static const tiny_hsm_state_descriptor_t write_hsm_state_descriptors[] = {
  { .state = write_state_top, .parent = nullptr },
  { .state = state_ready, .parent = write_state_top },
  { .state = state_writing, .parent = write_state_top }
};
static const tiny_hsm_configuration_t write_hsm_configuration = {
  .states = write_hsm_state_descriptors,
  .state_count = element_count(write_hsm_state_descriptors)
};

void erd_write_bridge_init(
  erd_write_bridge_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  erd_cache_t* cache,
  uint8_t host_address)
{
  self->timer_group = timer_group;
  self->erd_client = erd_client;
  self->mqtt_client = mqtt_client;
  self->cache = cache;
  self->erd_host_address = host_address;
  self->pending_request_id = 0;
  self->pending_erd = 0;
  self->pending_board_address = tiny_gea_broadcast_address;

  tiny_event_subscription_init(
    &self->mqtt_write_request_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<erd_write_bridge_t*>(context);
      tiny_hsm_send_signal(&self->hsm, signal_write_requested, _args);
    });
  tiny_event_subscribe(mqtt_client_on_write_request(mqtt_client), &self->mqtt_write_request_subscription);

  // Subscribe to ERD client activity for write results
  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<erd_write_bridge_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      switch(args->type) {
        case tiny_gea3_erd_client_activity_type_write_completed:
          tiny_hsm_send_signal(&self->hsm, signal_write_completed, args);
          break;
        case tiny_gea3_erd_client_activity_type_write_failed:
          tiny_hsm_send_signal(&self->hsm, signal_write_failed, args);
          break;
        default:
          break;
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  tiny_hsm_init(&self->hsm, &write_hsm_configuration, state_ready);
}

void erd_write_bridge_destroy(erd_write_bridge_t* self)
{
  // Guard against destroy() being called on a never-initialized struct.
  if(!self->timer_group) {
    return;
  }

  // Guard against partial init where mqtt_client or erd_client may be null.
  if (self->mqtt_client) {
    tiny_event_unsubscribe(mqtt_client_on_write_request(self->mqtt_client),
      &self->mqtt_write_request_subscription);
  }
  if (self->erd_client) {
    tiny_event_unsubscribe(tiny_gea3_erd_client_on_activity(self->erd_client),
      &self->erd_client_activity_subscription);
  }
}

void erd_write_bridge_set_host_address(erd_write_bridge_t* self, uint8_t host_address)
{
  self->erd_host_address = host_address;
}
