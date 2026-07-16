/*!
 * @file
 * @brief ERD polling bridge implementation.
 *
 * The polling bridge probes a pre-built list of ERDs at a known host
 * address, then settles into steady-state polling.  It does not perform
 * broadcast discovery — that is the responsibility of
 * DeviceIdentityManager/AutodiscoveryManager.
 *
 * State machine:
 *   state_probe_list  (reads each probe_list ERD; only successful reads added)
 *     → state_polling
 */

#include "erd_bridge_poll.h"

#include "erd_bridge_common.h"
#include "erd_lists.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include "erd_cache.h"

GEA_TAG(TAG) = "erd_bridge_poll";

// ============================================================================
// Polling bridge — forward declarations
// ============================================================================

static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_probe_list(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_failed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static void send_poll_read_requests_bounded(erd_bridge_poll_t* self, uint32_t budget_ms);
static bool send_cycle_reads(erd_bridge_poll_t* self);
static constexpr uint32_t POLL_YIELD_MS = 50;          // per-batch time budget
static constexpr uint32_t POLL_CYCLE_SEND_BUDGET_MS = 100;  // max time per send invocation
static constexpr uint32_t POLL_CYCLE_RESUME_MS = 100;   // timer interval when send budget exceeded

// ============================================================================
// Polling bridge — private helpers
// ============================================================================

static void arm_polling_timer(erd_bridge_poll_t* self, tiny_timer_ticks_t ticks)
{
  self->polling_timer_armed = true;
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

/* Fixed-capacity polling list — no dynamic reallocation.
 * POLLING_LIST_MAX_SIZE is defined in erd_lists.h (included via erd_bridge_common.h). */
static void add_erd_to_polling_list(erd_bridge_poll_t* self, tiny_erd_t erd)
{
  if (erd_set_contains(&self->erd_set, erd)) {
    return;
  }
  erd_set_insert(&self->erd_set, erd);
  if (self->polling_list_count < POLLING_LIST_MAX_SIZE) {
    self->erd_polling_list[self->polling_list_count] = erd;
    self->polling_list_count++;
  }
}

/* Reset the polling bridge's discovery state (erd_set and polling list).
 * Called from state_probe_list on re-entry after appliance lost so that
 * a new discovery phase always starts from a clean slate.
 *
 * Does NOT clear the shared erd_cache — that cache may be shared with the
 * subscription bridge.  Clearing it here would destroy subscription data
 * when the polling bridge re-discovers after appliance loss. */
static void clear_discovery_state(erd_bridge_poll_t* self)
{
  erd_set_clear(&self->erd_set);
  self->polling_list_count = 0;
}

// Called when all ERDs in the current polling cycle have responded.
// Records cycle metrics and decides whether to start the next cycle
// immediately or wait for the polling timer.  When 'immediate' is true,
// the next cycle starts right away (used when restart_pending is set or
// the timer has already expired).  When 'immediate' is false, only arms
// the timer if it isn't already armed (used from send_next_poll_read_request
// where restart_pending is never set).
static void on_polling_cycle_complete(erd_bridge_poll_t* self, bool immediate)
{
  uint32_t now = esphome::millis();
  self->last_cycle_time_ms = (uint32_t)(now - self->cycle_start_ms);
  self->cycle_count++;

  // Track consecutive cycle failures. If any ERD in the cycle failed,
  // increment the failure counter. On success, reset it.
  if (self->cycle_has_failure) {
    self->polling_failure_count++;
  } else {
    self->polling_failure_count = 0;
  }
  self->cycle_has_failure = false;

  if (immediate) {
    self->restart_pending = false;
    self->erd_index = 0;
    self->cycle_completed_count = 0;
    self->cycle_start_ms = now;
    if (send_cycle_reads(self)) {
      arm_polling_timer(self, self->polling_interval_ms);
    } else {
      arm_polling_timer(self, POLL_CYCLE_RESUME_MS);
    }
  } else if (!self->polling_timer_armed) {
    arm_polling_timer(self, self->polling_interval_ms);
  }
  // else: timer still armed — wait for it to fire and restart.
}

/* Send the next discovery-phase read request.  This operates on
 * appliance_erd_list / appliance_erd_list_count — NOT on erd_polling_list.
 * It is used by state_probe_list to advance one ERD at a time during
 * the probe phase.  For steady-state polling, see send_next_poll_read_request.
 *
 * Uses post-increment: erd_index points to the current ERD before the call,
 * and is advanced after the read is queued.  Initialized to 0 on entry. */
static bool send_next_read_request(erd_bridge_poll_t* self)
{
  reset_lost_appliance_timer(self);
  bool more_erds_to_try = (self->erd_index < self->appliance_erd_list_count);
  if (more_erds_to_try) {
    self->request_id++;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    self->erd_index++;
  }
  return more_erds_to_try;
}

static void send_next_poll_read_request(erd_bridge_poll_t* self)
{
  if (self->erd_index < self->polling_list_count) {
    self->request_id++;
    uint32_t t0 = esphome::millis();
    bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->erd_polling_list[self->erd_index]);
    uint32_t elapsed = esphome::millis() - t0;
    self->erd_index++;
    if (!queued) {
      /* Queue full — the read was silently dropped. Count it as completed
       * (failed) so the cycle can advance. The ERD will be retried on the
       * next cycle. */
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self, false);
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
//       esphome::delay(0); // yield to main loop
//     }
// so that all ERDs are queued before proceeding.

static void send_poll_read_requests_bounded(erd_bridge_poll_t* self, uint32_t budget_ms)
{
  uint32_t start = esphome::millis();
  uint16_t count = 0;
  while (self->erd_index < self->polling_list_count) {
    send_next_poll_read_request(self);
    count++;
    // Check time budget every 10 ERDs to minimize overhead on small lists.
    if (count % 10 == 0 && (esphome::millis() - start) >= budget_ms) {
      break;
    }
  }
}

/* Send cycle read requests within a time budget.  Does NOT arm any timer —
 * the caller is responsible for arming the next timer based on the return
 * value.  This prevents a single blocking call from holding the main loop
 * long enough to trigger the ESP32 task watchdog timer.
 *
 * Returns true if all reads were sent within budget, false if the budget
 * was exceeded and a resume timer should be armed. */
static bool send_cycle_reads(erd_bridge_poll_t* self)
{
  uint32_t send_start = esphome::millis();
  while (self->erd_index < self->polling_list_count) {
    send_poll_read_requests_bounded(self, POLL_YIELD_MS);
    esphome::delay(0);
    if ((esphome::millis() - send_start) >= POLL_CYCLE_SEND_BUDGET_MS) {
      self->cycle_sending_in_progress = true;
      return false;
    }
  }
  /* All reads sent. */
  self->cycle_sending_in_progress = false;
  uint32_t elapsed = esphome::millis() - send_start;
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
    // Signal with no payload (entry, exit, or unknown).  Callers are
    // responsible for handling entry before reaching this point.
    return tiny_hsm_result_signal_deferred;
  }
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case signal_read_completed:
      add_erd_to_polling_list(self, args->read_completed.erd);
      erd_cache_update(self->erd_cache, args->read_completed.erd,
              reinterpret_cast<const uint8_t*>(args->read_completed.data),
              args->read_completed.data_size);
      if (!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, state_polling);
      }
      break;

    case signal_read_failed:
      // Both not_supported and retries_exhausted are definitive — the GEA client
      // has finished its work.  Exclude the ERD from the polling list by simply
      // not adding it.
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
      // Restore the known host address and re-probe from scratch.
      // The caller always initializes the bridge with a real host address
      // (from autodiscovery); broadcast discovery is the responsibility of
      // DeviceIdentityManager/AutodiscoveryManager, not the polling bridge.
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
    // Clear discovery state on re-entry after appliance lost.
    if (self->polling_list_count > 0) {
      clear_discovery_state(self);
    }
    ESP_LOGI(TAG, "Polling bridge: %u ERDs to verify, interval %lu ms",
        self->probe_list_count, (unsigned long)self->polling_interval_ms);
    if (self->probe_list_count > 0) {
      send_next_read_request(self);
    } else {
      self->polling_list_complete = true;
      tiny_hsm_transition(hsm, state_polling);
    }
    return tiny_hsm_result_signal_consumed;
  }

  // Phase 2 verification: any failure — whether explicitly rejected
  // (not_supported) or timed out after all retries (retries_exhausted) —
  // permanently excludes the ERD from the Phase 3 polling list.
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
      self->restart_pending = false;
      arm_polling_timer(self, self->polling_interval_ms);
      self->polling_list_complete = true;
      self->current_state = polling_state_polling;
      ESP_LOGI(TAG, "Entered steady-state polling: %u ERDs, interval %lu ms",
               self->polling_list_count, (unsigned long)self->polling_interval_ms);
      // Notify startup HSM that discovery is complete.  Safe to call
      // synchronously from inside the polling HSM's state entry because:
      // 1. The callback sends a signal to the *startup* HSM (a different
      //    instance), not the polling HSM.
      // 2. startup_state_subscription_watch entry gates custom ERD polling
      //    on the subscription quiet window, which has not elapsed yet, so
      //    maybe_start_custom_erd_polling() returns early without touching
      //    the polling bridge.
      // 3. startup_state_running entry only logs.
      // Invariant: the callback must never trigger a path that sends a
      // signal back to the polling HSM while this entry handler runs.
      if (self->on_discovery_complete != nullptr) {
        self->on_discovery_complete(self->on_discovery_complete_context);
      }
      break;
    case signal_polling_timer_expired: {
      // Timer fired: mark as no longer armed.
      self->polling_timer_armed = false;
      if (self->erd_index >= self->polling_list_count && self->cycle_completed_count < self->polling_list_count) {
        // Reads are in flight (erd_index reached the end of the list) but
        // not all responses have arrived yet.  Per Phase 3 spec, let the
        // current cycle finish naturally before restarting.  If erd_index
        // Mark restart as pending so the cycle-completion handler kicks off
        // the next cycle as soon as the last ERD responds.
        self->restart_pending = true;
        break;
      }
      // If we were in the middle of sending reads for a cycle, resume.
      bool all_sent;
      if (self->cycle_sending_in_progress) {
        all_sent = send_cycle_reads(self);
        if (all_sent) {
          arm_polling_timer(self, self->polling_interval_ms);
        } else {
          arm_polling_timer(self, POLL_CYCLE_RESUME_MS);
        }
      } else {
        /* Cycle was already complete when the timer fired — start next cycle. */
        self->erd_index = 0;
        self->cycle_completed_count = 0;
        uint32_t now = esphome::millis();
        self->cycle_start_ms = now;
        all_sent = send_cycle_reads(self);
        if (all_sent) {
          arm_polling_timer(self, self->polling_interval_ms);
        } else {
          arm_polling_timer(self, POLL_CYCLE_RESUME_MS);
        }
      }
      break;
    }

    case signal_read_completed: {
      reset_lost_appliance_timer(self);
      tiny_erd_t      erd       = args->read_completed.erd;
      const uint8_t*  erd_data  = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t         data_size = args->read_completed.data_size;

      if (erd_set_contains(&self->erd_set, erd)) {
        // ERD already known — just update cache.
      } else {
        add_erd_to_polling_list(self, erd);
      }

      erd_cache_update(self->erd_cache, erd, erd_data, data_size);
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self, self->restart_pending || !self->polling_timer_armed);
      }
      break;
    }
    case signal_read_failed: {
      reset_lost_appliance_timer(self);
      ESP_LOGD(TAG, "Read failed for ERD 0x%04x", args->read_failed.erd);
      self->cycle_has_failure = true;
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self, self->restart_pending || !self->polling_timer_armed);
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
      // Appliance came back — re-probe from scratch.
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

// ============================================================================
// Polling bridge — HSM configuration
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

// ============================================================================
// Polling bridge — public API
// ============================================================================
// Shared initialization helper.  Sets self->erd_host_address and
// self->probe_list BEFORE calling tiny_hsm_init(), so that
// state_probe_list entry can probe at the correct address.
static void erd_bridge_poll_init_impl(
  erd_bridge_poll_t*    self,
  tiny_timer_group_t*       timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint32_t                  polling_interval_ms,
  uint8_t                   initial_host_address,
  const tiny_erd_t*         probe_list,
  uint16_t                  probe_list_count,
  erd_cache_t*              cache)
{
  self->timer_group            = timer_group;
  self->erd_client             = erd_client;
  self->polling_interval_ms    = polling_interval_ms;
  // Must be set before tiny_hsm_init() so state_probe_list entry
  // can probe at the correct address.
  self->erd_host_address       = initial_host_address;
  // Store the pre-known address so that signal_appliance_lost can restore it
  // after a transient read failure.
  self->known_host_address     = initial_host_address;
  self->probe_list             = probe_list;
  self->probe_list_count       = probe_list_count;
  self->polling_list_count     = 0;
  self->restart_pending             = false;
  self->cycle_sending_in_progress   = false;
  self->polling_timer_armed         = false;
  self->current_state             = polling_state_none;
  self->polling_list_complete       = false;
  self->cycle_start_ms              = 0;
  self->last_cycle_time_ms          = 0;
  self->cycle_count                 = 0;
  self->polling_failure_count     = 0;
  self->cycle_has_failure         = false;
  erd_set_init(&self->erd_set);
  self->erd_cache = cache;
  // Preserve a pre-set callback (e.g., from the bridge wiring it before
  // init to avoid a race when discovery completes synchronously).
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
  const tiny_erd_t*         probe_list,
  uint16_t                  probe_list_count,
  erd_cache_t*              cache)
{
  erd_bridge_poll_init_impl(
    self, timer_group, erd_client, polling_interval_ms,
    host_address, probe_list, probe_list_count, cache);
}

void erd_bridge_poll_destroy(erd_bridge_poll_t* self)
{
  /* Guard against destroy() being called on a never-initialized struct (e.g.
   * in test teardowns that always call both bridge and polling destroy). */
  if (!self->timer_group) {
    return;
  }

  /* Stop all active timers so they cannot fire after the bridge is torn down.
   * tiny_timer_stop() is idempotent: safe to call even if a timer is not active. */
  tiny_timer_stop(self->timer_group, &self->appliance_lost_timer);
  tiny_timer_stop(self->timer_group, &self->polling_timer);

  /* Remove event subscription before freeing heap state.
   * Guard against partial init where erd_client may be null. */
  if (self->erd_client) {
    tiny_event_unsubscribe(
      tiny_gea3_erd_client_on_activity(self->erd_client),
      &self->erd_client_activity_subscription);
  }

  /* erd_set and erd_polling_list are fixed arrays embedded in the struct —
   * no heap cleanup needed. */
  self->current_state = polling_state_none;
}
