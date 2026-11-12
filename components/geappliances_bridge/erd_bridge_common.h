
#pragma once

// All functions are template or `inline` so each translation unit gets its own
// copy without ODR violations.

#include <stdio.h>
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

enum {
  resubscribe_delay = 1000,
  subscription_retention_period = 30 * 1000,
  subscription_quiet_period = 2 * 1000,
  appliance_lost_timeout = 60000
};

#define PROBE_ENTRY_DEFAULT_ADDRESS 0xFF

typedef struct {
  tiny_erd_t erd;
  uint8_t board_address;
} probe_entry_t;

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

// Board distribution of an ERD list, used by the one-line boot summaries.
// Only the first BOARD_TALLY_MAX addresses are named; entries on any further
// board are counted into `untracked` so the formatted line stays bounded on
// multi-board appliances instead of growing with the board count.
#define BOARD_TALLY_MAX 4

typedef struct {
  uint8_t address[BOARD_TALLY_MAX];
  uint16_t count[BOARD_TALLY_MAX];
  uint16_t untracked;
} board_tally_t;

static inline void board_tally_init(board_tally_t* self)
{
  memset(self, 0, sizeof(*self));
}

static inline void board_tally_add(board_tally_t* self, uint8_t address)
{
  for (uint8_t i = 0; i < BOARD_TALLY_MAX; i++) {
    if (self->count[i] == 0) {
      self->address[i] = address;
      self->count[i] = 1;
      return;
    }
    if (self->address[i] == address) {
      self->count[i]++;
      return;
    }
  }
  self->untracked++;
}

// Longest possible tally line: BOARD_TALLY_MAX "0xNN 65535" tokens, their
// separators, and the "+65535 more" tail.
#define BOARD_TALLY_BUF_SIZE 64

/*!
 * Format as "primary 41, 0xC0 24" (PROBE_ENTRY_DEFAULT_ADDRESS is the primary
 * board sentinel, so it reads as "primary", not "0xFF"). An empty tally is
 * "none"; boards past BOARD_TALLY_MAX collapse into "+N more".
 *
 * Never writes past out_len and always NUL-terminates. A buffer shorter than
 * BOARD_TALLY_BUF_SIZE truncates wherever snprintf reached, so callers pass
 * BOARD_TALLY_BUF_SIZE to get the whole line. A non-empty tally never renders
 * as "none" - that would read as "no boards" on a multi-board appliance.
 * Returns the number of characters stored, excluding the terminator.
 */
static inline int board_tally_format(const board_tally_t* self, char* out, size_t out_len)
{
  if (out_len == 0) {
    return 0;
  }
  if (self->count[0] == 0 && self->untracked == 0) {
    return snprintf(out, out_len, "none");
  }

  int written = 0;
  for (uint8_t i = 0; i < BOARD_TALLY_MAX && self->count[i] > 0; i++) {
    const int room = (int) out_len - written;
    if (room <= 1) {
      break;
    }
    const char* label = (self->address[i] == PROBE_ENTRY_DEFAULT_ADDRESS)
        ? "primary" : nullptr;
    const int n = (label != nullptr)
        ? snprintf(out + written, room, "%s%s %u",
                   (written > 0) ? ", " : "", label, self->count[i])
        : snprintf(out + written, room, "%s0x%02X %u",
                   (written > 0) ? ", " : "", self->address[i], self->count[i]);
    if (n < 0) {
      break;
    }
    if (n >= room) {
      return (int) out_len - 1;  // snprintf truncated and NUL-terminated within room
    }
    written += n;
  }
  if (self->untracked > 0) {
    const int room = (int) out_len - written;
    if (room > 1) {
      const int n = snprintf(out + written, room, "%s+%u more",
                             (written > 0) ? ", " : "", self->untracked);
      if (n < 0) {
        return written;
      }
      if (n >= room) {
        return (int) out_len - 1;
      }
      written += n;
    }
  }
  return written;
}
