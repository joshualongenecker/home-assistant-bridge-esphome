#include "erd_bridge_poll.h"

#include "erd_bridge_common.h"
#include "erd_lists.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include "erd_cache.h"

GEA_TAG(TAG) = "erd_bridge_poll";

static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_probe_list(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_failed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static void send_poll_read_requests_bounded(erd_bridge_poll_t* self, uint32_t budget_ms);
static bool send_cycle_reads(erd_bridge_poll_t* self);
static constexpr uint32_t POLL_YIELD_MS = 50;
static constexpr uint32_t POLL_CYCLE_SEND_BUDGET_MS = 100;
static constexpr uint32_t POLL_CYCLE_RESUME_MS = 100;

static void poll_default_yield(void)
{
  esphome::delay(0);
}

static void arm_polling_timer(erd_bridge_poll_t* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start(
    self->timer_group, &self->polling_timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<erd_bridge_poll_t*>(context)->hsm, signal_polling_timer_expired, nullptr);
    });
}

static void reset_lost_appliance_timer(erd_bridge_poll_t* self)
{
  tiny_timer_stop(self->timer_group, &self->appliance_lost_timer);
  tiny_timer_start(
    self->timer_group, &self->appliance_lost_timer, appliance_lost_timeout, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<erd_bridge_poll_t*>(context)->hsm, signal_appliance_lost, nullptr);
    });
}

static void add_erd_to_polling_list(erd_bridge_poll_t* self, tiny_erd_t erd, uint8_t address)
{
  for (uint16_t i = 0; i < self->polling_list_count; i++) {
    if (self->erd_polling_list[i] == erd && self->erd_polling_addresses[i] == address) {
      return;
    }
  }
  if (self->polling_list_count < POLLING_LIST_MAX_SIZE) {
    self->erd_polling_list[self->polling_list_count] = erd;
    self->erd_polling_addresses[self->polling_list_count] = address;
    self->polling_list_count++;
    erd_set_insert(&self->erd_set, erd);
  }
}

static void clear_discovery_state(erd_bridge_poll_t* self)
{
  erd_set_clear(&self->erd_set);
  self->polling_list_count = 0;
}

static void set_cycle_phase(erd_bridge_poll_t* self, poll_cycle_phase_t phase)
{
  self->cycle_phase = phase;
}

static void drive_cycle_sends(erd_bridge_poll_t* self)
{
  bool all_sent = send_cycle_reads(self);
  if (self->cycle_phase == poll_cycle_phase_idle) {
    arm_polling_timer(self, self->polling_interval_ms);
    return;
  }
  if (all_sent) {
    set_cycle_phase(self, poll_cycle_phase_awaiting);
    arm_polling_timer(self, self->polling_interval_ms);
  } else {
    arm_polling_timer(self, POLL_CYCLE_RESUME_MS);
  }
}

static void on_polling_cycle_complete(erd_bridge_poll_t* self)
{
  uint32_t now = self->get_time_ms();
  self->last_cycle_time_ms = (uint32_t)(now - self->cycle_start_ms);
  self->cycle_count++;

  if (self->cycle_has_failure) {
    self->polling_failure_count++;
  } else {
    self->polling_failure_count = 0;
  }
  self->cycle_has_failure = false;

  switch (self->cycle_phase) {
    case poll_cycle_phase_awaiting:
      set_cycle_phase(self, poll_cycle_phase_idle);
      break;
    case poll_cycle_phase_awaiting_restart:
      self->erd_index = 0;
      self->cycle_completed_count = 0;
      self->cycle_start_ms = now;
      drive_cycle_sends(self);
      break;
    case poll_cycle_phase_sending:
      set_cycle_phase(self, poll_cycle_phase_idle);
      break;
    case poll_cycle_phase_idle:
      break;
  }
}

static bool send_next_read_request(erd_bridge_poll_t* self)
{
  reset_lost_appliance_timer(self);
  bool more_erds_to_try = (self->erd_index < self->appliance_erd_list_count);
  if (more_erds_to_try) {
    self->request_id++;
    probe_entry_t entry = self->appliance_erd_list[self->erd_index];
    uint8_t addr = (entry.board_address == PROBE_ENTRY_DEFAULT_ADDRESS)
                  ? self->erd_host_address : entry.board_address;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, addr, entry.erd);
    self->erd_index++;
  }
  return more_erds_to_try;
}

static void send_next_poll_read_request(erd_bridge_poll_t* self)
{
  if (self->erd_index < self->polling_list_count) {
    self->request_id++;
    uint8_t addr = self->erd_polling_addresses[self->erd_index];
    if (addr == PROBE_ENTRY_DEFAULT_ADDRESS) addr = self->erd_host_address;
    uint32_t t0 = self->get_time_ms();
    bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, addr, self->erd_polling_list[self->erd_index]);
    uint32_t elapsed = self->get_time_ms() - t0;
    self->erd_index++;
    if (!queued) {
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self);
      }
    }
    if (elapsed >= 500) {
      ESP_LOGW(TAG, "Slow read request: %lums for ERD 0x%04x (queued=%s)",
               (unsigned long)elapsed, self->erd_polling_list[self->erd_index - 1], queued ? "yes" : "no");
    }
  }
}

// Send remaining poll read requests with a time budget to avoid blocking
// the ESPHome main loop for too long.  With large ERD lists (100+),
// sending all reads synchronously can take hundreds of milliseconds.
// This helper processes in batches, yielding after POLL_YIELD_MS so the
// framework can service other tasks.
//
// POLL_YIELD_MS — per-batch time budget in milliseconds.  Values below 20 ms
// risk excessive context-switch overhead; values above 100 ms may cause
// noticeable blocking for other ESPHome components.  Tune based on appliance
// ERD count and hardware platform.
//
// The caller wraps this in a while loop:
//     while (self->erd_index < self->polling_list_count) {
//       send_poll_read_requests_bounded(self, POLL_YIELD_MS);
//       self->yield_fn(); // yield to main loop
//     }
// so that all ERDs are queued before proceeding.

static void send_poll_read_requests_bounded(erd_bridge_poll_t* self, uint32_t budget_ms)
{
  uint32_t start = self->get_time_ms();
  uint16_t count = 0;
  while (self->erd_index < self->polling_list_count) {
    send_next_poll_read_request(self);
    count++;
    if (count % 10 == 0 && (self->get_time_ms() - start) >= budget_ms) {
      break;
    }
  }
}

static bool send_cycle_reads(erd_bridge_poll_t* self)
{
  set_cycle_phase(self, poll_cycle_phase_sending);
  uint32_t send_start = self->get_time_ms();
  while (self->erd_index < self->polling_list_count) {
    send_poll_read_requests_bounded(self, POLL_YIELD_MS);
    self->yield_fn();
    if ((self->get_time_ms() - send_start) >= POLL_CYCLE_SEND_BUDGET_MS) {
      return false;
    }
  }
  if (self->cycle_completed_count >= self->polling_list_count) {
    set_cycle_phase(self, poll_cycle_phase_idle);
  } else {
    set_cycle_phase(self, poll_cycle_phase_awaiting);
  }
  uint32_t elapsed = self->get_time_ms() - send_start;
  if (elapsed >= 1000) {
    ESP_LOGW(TAG, "Long cycle send: %lums for %u ERDs", (unsigned long)elapsed, self->polling_list_count);
  }
  return true;
}

/* Shared handler for discovery read signals, used by state_probe_list.
 * Handles signal_read_completed by adding the ERD to the polling list
 * and cache, then advancing to the next ERD.  When all ERDs are
 * exhausted, transitions to state_polling.
 *
 * Contract: callers MUST handle tiny_hsm_signal_entry before delegating here.
 * This handler only processes signal_read_completed and signal_read_failed, both
 * of which carry non-null data from the GEA client activity callback.  Any signal
 * with null data (entry, exit, etc.) is deferred. */
static tiny_hsm_result_t handle_discovery_list_signals(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);
  if (data == nullptr) {
    return tiny_hsm_result_signal_deferred;
  }
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case signal_read_completed: {
      tiny_erd_t erd = args->read_completed.erd;
      uint8_t addr = args->address;
      add_erd_to_polling_list(self, erd, addr);
      erd_cache_update(self->erd_cache, erd, addr,
              reinterpret_cast<const uint8_t*>(args->read_completed.data),
              args->read_completed.data_size);
      if (!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, state_polling);
      }
      break;
    }

    case signal_read_failed:
      if (!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, state_polling);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}
static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, [[maybe_unused]] const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);

  switch (signal) {
    case signal_appliance_lost:
      self->erd_host_address = self->known_host_address;
      tiny_hsm_transition(hsm, state_probe_list);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_probe_list(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  if (signal == tiny_hsm_signal_entry) {
    self->current_state = polling_state_probing;
    self->appliance_erd_list = self->probe_list;
    self->appliance_erd_list_count = self->probe_list_count;
    self->erd_index = 0;
    if (self->polling_list_count > 0) {
      clear_discovery_state(self);
    }
    ESP_LOGI(TAG, "Polling bridge: %u ERDs to verify, interval %lu ms",
        self->probe_list_count, (unsigned long)self->polling_interval_ms);
    if (self->probe_list_count > 0) {
      send_next_read_request(self);
    } else {
      tiny_hsm_transition(hsm, state_polling);
    }
    return tiny_hsm_result_signal_consumed;
  }

  if (signal == signal_read_failed) {
    erd_set_insert(&self->erd_set, args->read_failed.erd);
    if (args->read_failed.reason == tiny_gea3_erd_client_read_failure_reason_retries_exhausted) {
      ESP_LOGD(TAG, "Probe: ERD 0x%04x timed out after all retries, excluded from polling list",
               args->read_failed.erd);
    }
    if (!send_next_read_request(self)) {
      tiny_hsm_transition(hsm, state_polling);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case tiny_hsm_signal_entry:
      self->erd_index = 0;
      self->cycle_completed_count = 0;
      set_cycle_phase(self, poll_cycle_phase_idle);
      arm_polling_timer(self, self->polling_interval_ms);
      self->current_state = polling_state_polling;
      ESP_LOGI(TAG, "Entered steady-state polling: %u ERDs, interval %lu ms",
               self->polling_list_count, (unsigned long)self->polling_interval_ms);
      if (self->on_discovery_complete != nullptr) {
        self->on_discovery_complete(self->on_discovery_complete_context);
      }
      break;
    case signal_polling_timer_expired: {
      switch (self->cycle_phase) {
        case poll_cycle_phase_idle:
          self->erd_index = 0;
          self->cycle_completed_count = 0;
          self->cycle_start_ms = self->get_time_ms();
          drive_cycle_sends(self);
          break;
        case poll_cycle_phase_sending:
          drive_cycle_sends(self);
          break;
        case poll_cycle_phase_awaiting:
          set_cycle_phase(self, poll_cycle_phase_awaiting_restart);
          break;
        case poll_cycle_phase_awaiting_restart:
          break;
      }
      break;
    }

    case signal_read_completed: {
      reset_lost_appliance_timer(self);
      tiny_erd_t      erd       = args->read_completed.erd;
      const uint8_t*  erd_data  = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t         data_size = args->read_completed.data_size;

      uint8_t addr = args->address;

      if (erd_set_contains(&self->erd_set, erd)) {
      } else {
        add_erd_to_polling_list(self, erd, addr);
      }

      erd_cache_update(self->erd_cache, erd, addr, erd_data, data_size);
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self);
      }
      break;
    }
    case signal_read_failed: {
      reset_lost_appliance_timer(self);
      ESP_LOGD(TAG, "Read failed for ERD 0x%04x", args->read_failed.erd);
      self->cycle_has_failure = true;
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self);
        if (self->polling_failure_count >= 3) {
          ESP_LOGE(TAG, "Polling bridge failed after %u consecutive failed cycles",
                   self->polling_failure_count);
          tiny_hsm_transition(hsm, state_failed);
        }
      }
      break;
    }

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_failed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      self->current_state = polling_state_failed;
      ESP_LOGE(TAG, "Polling bridge failed after %u consecutive failed cycles",
               self->polling_failure_count);
      break;

    case signal_appliance_lost:
      self->erd_host_address = self->known_host_address;
      self->polling_failure_count = 0;
      tiny_hsm_transition(hsm, state_probe_list);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static const tiny_hsm_state_descriptor_t poll_hsm_state_descriptors[] = {
  { .state = poll_state_top,              .parent = nullptr         },
  { .state = state_probe_list,            .parent = poll_state_top  },
  { .state = state_polling,               .parent = poll_state_top  },
  { .state = state_failed,                .parent = poll_state_top  }
};
static const tiny_hsm_configuration_t poll_hsm_configuration = {
  .states      = poll_hsm_state_descriptors,
  .state_count = element_count(poll_hsm_state_descriptors)
};

static void erd_bridge_poll_init_impl(
  erd_bridge_poll_t*    self,
  tiny_timer_group_t*       timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint32_t                  polling_interval_ms,
  uint8_t                   initial_host_address,
  const probe_entry_t*      probe_list,
  uint16_t                  probe_list_count,
  erd_cache_t*              cache)
{
  self->timer_group            = timer_group;
  self->erd_client             = erd_client;
  self->polling_interval_ms    = polling_interval_ms;
  self->erd_host_address       = initial_host_address;
  self->known_host_address     = initial_host_address;
  self->probe_list             = probe_list;
  self->probe_list_count       = probe_list_count;
  self->polling_list_count     = 0;
  self->cycle_phase               = poll_cycle_phase_idle;
  self->current_state             = polling_state_none;
  self->cycle_start_ms              = 0;
  self->last_cycle_time_ms          = 0;
  self->cycle_count                 = 0;
  self->polling_failure_count     = 0;
  self->cycle_has_failure         = false;
  self->get_time_ms = esphome::millis;
  self->yield_fn = poll_default_yield;
  erd_set_init(&self->erd_set);
  self->erd_cache = cache;
  if (self->on_discovery_complete == nullptr) {
    self->on_discovery_complete_context = nullptr;
  }

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<erd_bridge_poll_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      switch (args->type) {
        case tiny_gea3_erd_client_activity_type_read_completed:
          tiny_hsm_send_signal(&self->hsm, signal_read_completed, args);
          break;
        case tiny_gea3_erd_client_activity_type_read_failed:
          tiny_hsm_send_signal(&self->hsm, signal_read_failed, args);
          break;
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  tiny_hsm_init(&self->hsm, &poll_hsm_configuration, state_probe_list);
}

void erd_bridge_poll_init(
  erd_bridge_poll_t*    self,
  tiny_timer_group_t*       timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint32_t                  polling_interval_ms,
  uint8_t                   host_address,
  const probe_entry_t*      probe_list,
  uint16_t                  probe_list_count,
  erd_cache_t*              cache)
{
  erd_bridge_poll_init_impl(
    self, timer_group, erd_client, polling_interval_ms,
    host_address, probe_list, probe_list_count, cache);
}

void erd_bridge_poll_set_time_fn(
  erd_bridge_poll_t* self,
  uint32_t (*get_time_ms)(void))
{
  self->get_time_ms = get_time_ms ? get_time_ms : esphome::millis;
}

void erd_bridge_poll_set_yield_fn(
  erd_bridge_poll_t* self,
  void (*yield_fn)(void))
{
  self->yield_fn = yield_fn ? yield_fn : poll_default_yield;
}

void erd_bridge_poll_destroy(erd_bridge_poll_t* self)
{
  if (!self->timer_group) {
    return;
  }

  tiny_timer_stop(self->timer_group, &self->appliance_lost_timer);
  tiny_timer_stop(self->timer_group, &self->polling_timer);

  if (self->erd_client) {
    tiny_event_unsubscribe(
      tiny_gea3_erd_client_on_activity(self->erd_client),
      &self->erd_client_activity_subscription);
  }

  self->current_state = polling_state_none;
  self->cycle_phase = poll_cycle_phase_idle;
}
