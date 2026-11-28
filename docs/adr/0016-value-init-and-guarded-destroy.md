# ADR-0016: Embedded structs that own heap are value-initialized, and every destroy path is guarded and idempotent

Status: accepted (2026-11-27)
Related: ADR-0013, ADR-0015, `.github/copilot-instructions.md` (Critical Invariants)

## Context

`GeappliancesBridge` embeds several C structs that own heap:
`ha_discovery_manager_`, `erd_cache_publisher_`, `mqtt_client_adapter_`. Their
init and teardown functions act on whatever the struct currently holds:
`ha_discovery_manager_init()` and `ha_discovery_manager_cleanup()` both `free()`
the four run buffers *before* `memset()`ing the struct, and `teardown()`
dispatches on `erd_cache_publisher_.cache` before destroying it. If those fields
start out as allocator garbage, the destroy path frees an arbitrary pointer —
and a write through that pointer is invisible to AddressSanitizer when it lands
inside a live allocation.

Deployed firmware hides this completely: ESPHome instantiates the component as a
static global, which the loader zeroes. Every unit test does
`new GeappliancesBridge()`, so only the test heap ever sees the garbage.

## Decision

- **`ha_discovery_manager_`, `erd_cache_publisher_` and `mqtt_client_adapter_`
  carry `{}` in `geappliances_bridge.h`**, and function-pointer wrappers such as
  `i_mqtt_client_t` start `NULL` because their calls go straight through
  `self->api`.
- **Every destroy path guards on initialization state.** `erd_cache_destroy()`
  checks `self->initialized` before deleting `self->lock`; destroy functions
  tolerate a never-initialized struct (test teardowns call both the bridge and
  the polling destroy) and a partially-initialized one (where `mqtt_client` or
  `erd_client` may still be null).
- **Cleanup and destroy are idempotent**: safe to call more than once.
- **Destroy stops timers and removes event subscriptions before freeing heap
  state.** `tiny_timer_stop()` is idempotent, so it is safe to call for a timer
  that is not active. Subscription callbacks (for example
  `erd_client_activity_subscription`) reference the struct being freed, so they
  must be unregistered first or a callback can fire against freed memory.
- **The pattern-init build is the check that catches a missing `{}`**
  (ADR-0013): `make integration-test BUILD_DIR=./build-pattern
  EXTRA_CXXFLAGS=-ftrivial-auto-var-init=pattern` faults at the bad `free()`
  while the default build passes.

## Consequences

- A heap-allocated bridge — every test, and any future dynamic instantiation —
  is as safe as the static-global instance the device uses.
- Adding an owning field to an embedded struct means initializing it in the
  declaration or making the destroy path guard on it; the pattern-init gate
  fails the first option's omission, not the second's.
- Destroy ordering (stop timers → unsubscribe → free) is a contract, not a
  style preference.
- Local macOS runs do not catch this; the pattern-init variant must be built in
  CI, with its own `BUILD_DIR`.

## Code anchors

- `geappliances_bridge.h` — the `{}` initializers on the embedded structs.
- `geappliances_bridge.cpp` — `teardown()`, `destroy_data_plane_()`.
- `ha_discovery_manager.cpp` — `ha_discovery_manager_init()`,
  `ha_discovery_manager_cleanup()`, the four run buffers.
- `erd_cache.cpp` — `erd_cache_destroy()` and its `initialized` guard.
- `erd_bridge_poll.cpp` / `erd_bridge_subscribe.cpp` — destroy paths that stop
  timers and unsubscribe.
- `Makefile` / `.github/workflows/test.yml` — the pattern-init gate.
