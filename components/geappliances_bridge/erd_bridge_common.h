// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Provide shared timing constants, HSM signal identifiers, and utility
//       templates used by both erd_bridge_subscribe.cpp and erd_bridge_poll.cpp.
//
// Responsibilities:
//   - Declare signal enum values shared by both bridge implementations
//   - Define timing constants (resubscribe_delay, etc.)
//   - Provide arm_timer / disarm_timer helpers
//
// NOT responsible for:
//   - Any bridge state or lifecycle logic
//   - Anything not shared between both bridge implementations
//   - Any MQTT interaction (bridges write to erd_cache only)
//
// Dependencies:
//   - erd_bridge_subscribe.h, erd_bridge_poll.h, tiny_utils.h, tiny_gea_constants.h
// =============================================================================

#pragma once

/*!
 * @file
 * @brief Shared signals, timing constants, and utility templates used by both
 *        the subscription bridge (erd_bridge_subscribe.cpp) and the polling bridge
 *        (erd_bridge_poll.cpp).
 *
 * All functions are either template functions (implicitly inline) or declared
 * `inline` so that each translation unit gets its own copy without ODR violations.
 */

#include <string.h>
#include <cstdint>

extern "C" {
#include "tiny_erd.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"
#include "tiny_utils.h"
#include "tiny_gea_constants.h"
#include "erd_lists.h"
}

// ============================================================================
// Shared timing constants
// ============================================================================

enum {
  resubscribe_delay = 1000,
  subscription_retention_period = 30 * 1000,
  subscription_quiet_period = 2 * 1000,
  appliance_lost_timeout = 60000
};
// ============================================================================
// Probe entry type — ERD with optional per-ERD board address
// ============================================================================

#define PROBE_ENTRY_DEFAULT_ADDRESS 0xFF

typedef struct {
  tiny_erd_t erd;
  uint8_t board_address;
} probe_entry_t;

// ============================================================================
// Subscription state machine states
// ============================================================================

typedef enum {
  subscription_state_none,
  subscription_state_subscribing,
  subscription_state_subscribed,
  subscription_state_steady,
  subscription_state_failed
} subscription_state_t;

static inline const char* subscription_state_name(subscription_state_t state)
{
  switch(state) {
    case subscription_state_none: return nullptr;
    case subscription_state_subscribing: return "subscribing";
    case subscription_state_subscribed: return "subscribed";
    case subscription_state_steady: return "steady";
    case subscription_state_failed: return "failed";
    default: return "unknown";
  }
}

static inline bool subscription_is_active(subscription_state_t state)
{
  return (state != subscription_state_none) && (state != subscription_state_failed);
}

// ============================================================================
// Shared signal identifiers
// ============================================================================
enum {
  signal_timer_expired = tiny_hsm_signal_user_start,
  signal_polling_timer_expired,
  signal_subscription_failed,
  signal_subscription_added_or_retained,
  signal_subscription_host_came_online,
  signal_subscription_publication_received,
  signal_quiet_period_expired,
  signal_read_failed,
  signal_read_completed,
  signal_appliance_lost
};

// ============================================================================
// Fixed-capacity ERD set — replaces std::set<tiny_erd_t> to eliminate heap
// Sorted array with binary search; O(log n) lookups and
// O(n) inserts (n is small: bounded by probe list or subscription ERDs).
// ============================================================================

#define ERD_SET_CAPACITY POLLING_LIST_MAX_SIZE

typedef struct {
  tiny_erd_t data[ERD_SET_CAPACITY];
  uint16_t count;
} erd_set_t;

static inline void erd_set_init(erd_set_t* self)
{
  self->count = 0;
}

static inline bool erd_set_contains(erd_set_t* self, tiny_erd_t erd)
{
  uint16_t lo = 0;
  uint16_t hi = self->count;
  while (lo < hi) {
    uint16_t mid = lo + (hi - lo) / 2;
    if (self->data[mid] < erd) {
      lo = mid + 1;
    } else if (self->data[mid] > erd) {
      hi = mid;
    } else {
      return true;
    }
  }
  return false;
}

static inline bool erd_set_insert(erd_set_t* self, tiny_erd_t erd)
{
  /* Binary search for insertion position. */
  uint16_t lo = 0;
  uint16_t hi = self->count;
  while (lo < hi) {
    uint16_t mid = lo + (hi - lo) / 2;
    if (self->data[mid] < erd) {
      lo = mid + 1;
    } else if (self->data[mid] > erd) {
      hi = mid;
    } else {
      return false;  /* already present */
    }
  }
  if (self->count >= ERD_SET_CAPACITY) return false;
  /* Shift elements to make room at position lo. */
  for (int i = (int)self->count; i > (int)lo; i--) {
    self->data[i] = self->data[i - 1];
  }
  self->data[lo] = erd;
  self->count++;
  return true;
}

static inline void erd_set_clear(erd_set_t* self)
{
  self->count = 0;
}

// ============================================================================
// Shared utility templates
// ============================================================================

template<typename T>
static void arm_timer(T* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start(
    self->timer_group, &self->timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<T*>(context)->hsm, signal_timer_expired, nullptr);
    });
}

template<typename T>
static void disarm_timer(T* self)
{
  tiny_timer_stop(self->timer_group, &self->timer);
}
// ============================================================================
// Polling state machine states
// ============================================================================

typedef enum {
  polling_state_none,
  polling_state_probing,
  polling_state_polling,
  polling_state_failed
} polling_state_t;

static inline const char* polling_state_name(polling_state_t state)
{
  switch(state) {
    case polling_state_none: return nullptr;
    case polling_state_probing: return "probing";
    case polling_state_polling: return "polling";
    case polling_state_failed: return "failed";
    default: return "unknown";
  }
}
