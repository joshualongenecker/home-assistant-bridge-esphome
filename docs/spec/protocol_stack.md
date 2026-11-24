# ProtocolStack — Specification

## 1. Overview

### 1.1 Purpose

Own the wire-level protocol machinery — the UART adapters, the GEA2/GEA3 interfaces, both ERD clients, the GEA2→GEA3 adapter, their buffers, the GEA2 tick source, and the shared timer group — and drive it once per `loop()` iteration. Extracted from `GeappliancesBridge` (architecture review recommendation 1) so the bridge orchestrates data-plane policy while this class owns protocol mechanics.

### 1.2 Responsibilities

- Initialize the stack for zero, one, or two configured UARTs (`init()`), including the shared `tiny_timer_group_t` every data-path component registers timers on
- Drive one iteration per `loop()`: the GEA2 tight loop when GEA2 work is in flight, the GEA3 iteration otherwise, timer-only with no UART (`run(bool gea2_pending)`)
- Own the GEA2 millisecond catch-up tick counter and the `gea2_msec_interrupt_` event that advances the GEA2 interface's internal timers
- Expose the ERD clients, the UART adapters, and the timer group the orchestration layer wires up (subscriptions, bridges, managers)

### 1.3 Not Responsible For

- Deciding which protocol autodiscovery is using (`AutodiscoveryManager`) or which data-plane bridges exist (`GeappliancesBridge`'s `DataPlanePhase` machine)
- Routing ERD client activity to managers: the bridge subscribes its own event subscriptions to the exposed clients
- MQTT, the ERD cache, or any data-plane component (bridges, publishers, discovery)

---

## 2. Interface

### 2.1 Class

```cpp
class ProtocolStack {
 public:
  void init(uint8_t client_address,
            uart::UARTComponent* gea3_uart,
            uart::UARTComponent* gea2_uart);
  void teardown();
  void run(bool gea2_pending);
  void set_gea2_active(bool active);
  bool is_gea2_active() const;
  bool has_gea3_uart() const;
  bool has_gea2_uart() const;
  i_tiny_gea3_erd_client_t* get_gea3_client();
  i_tiny_gea2_erd_client_t* get_gea2_client();
  i_tiny_gea3_erd_client_t* get_gea2_adapter_client();
  esphome_uart_adapter_t* get_gea3_uart_adapter();
  esphome_uart_adapter_t* get_gea2_uart_adapter();
  tiny_timer_group_t* timer_group();
};
```

ESPHome applies configuration after the component is constructed, so the UART pointers arrive at `init()`, not at construction. The stack is a plain member of `GeappliancesBridge` (`protocol_stack_`), default-constructed, and re-initializable after `teardown()` (ESPHome re-setup).

### 2.2 Public API

| Method | Description |
|--------|-------------|
| `init(client_address, gea3_uart, gea2_uart)` | Initialize the timer group, adapters, interfaces, and ERD clients for the configured UARTs (zero, one, or two) |
| `teardown()` | Reset per-run state (GEA2 tick tracking, the GEA2-active latch) so a re-init starts fresh |
| `run(gea2_pending)` | Drive one iteration: GEA2 tight loop / GEA3 iteration / timer-only |
| `set_gea2_active(active)` | Sticky "GEA2 is the active protocol" latch (manual `device_id` configs with only a GEA2 UART) |
| `get_gea3_client()` / `get_gea2_client()` | The GEA3 / GEA2 ERD client interfaces; `nullptr` when the matching define is absent (requirement 3.5.2) |
| `get_gea2_adapter_client()` | The GEA2→GEA3 adapter client, which re-publishes GEA2 activity as GEA3-typed events; `nullptr` without `GEA_BRIDGE_HAS_GEA2` |
| `timer_group()` | The shared timer group every data-path component registers timers on |

---

## 3. Behavior

### 3.1 Iteration Dispatch

#### Requirement 3.1.1: GEA2 Tight Loop Gating

`run(gea2_pending)` MUST run the GEA2 tight loop when the GEA2 UART is configured AND (`gea2_pending` OR the GEA2-active latch is set). It MUST run the GEA3 iteration when the GEA3 UART is configured and the GEA2 tight loop is not selected, and the timer-only iteration when neither UART is configured.

**Rationale:** `gea2_pending` is orchestration-owned (the bridge computes it from autodiscovery state: `is_gea2_protocol()`, `AUTODISCOVERY_GEA2_BROADCAST_PENDING`, `AUTODISCOVERY_GEA2_BROADCAST_WAITING`); the stack ORs in its own latch because a manual `device_id` config with only a GEA2 UART has no autodiscovery to report progress. Gating on the UART being configured at all keeps a GEA3-only build out of the GEA2 path.

**Implementation:** `protocol_stack.cpp` `run()` — `need_gea2_loop = gea2_uart_ != nullptr && (gea2_pending || gea2_active_)`.

**Verification:** `test/tests/protocol_stack_test.cpp` — `run_with_both_uarts_gates_adapters_by_gea2_pending`, `gea2_pending_ignored_without_gea2_uart`, `run_with_gea2_active_latch_prefers_gea2`, `gea2_active_latch_ignored_without_gea2_uart`.

#### Requirement 3.1.2: Dual-UART Adapter Gating

When both UARTs are configured, `run()` MUST enable only the adapter for the active protocol (`esphome_uart_adapter_set_enabled`) so the inactive adapter's poll callback returns early without reading bytes or publishing events to an interface that isn't being driven.

**Rationale:** Both adapters register poll timers in the shared timer group; without gating, `tiny_timer_group_run()` fires both poll callbacks every iteration.

**Implementation:** `protocol_stack.cpp` `run()` — both adapters gated by `need_gea2_loop`.

**Verification:** `run_with_both_uarts_gates_adapters_by_gea2_pending` asserts the enabled flags on the test doubles.

### 3.2 GEA2 Millisecond Catch-Up

#### Requirement 3.2.1: Tick-Counter Time Source

The GEA2 interface's internal timer group MUST be driven by a tick counter that increments once per real millisecond inside the GEA2 tight loop, so the interface's internal timers advance by at most 1 ms per event regardless of the ~50 ms ESPHome framework gap between `loop()` calls.

**Rationale:** The GEA2 interface's ack/reflection/interbyte timeouts are 6–8 ms; a wall-clock-driven timer group that only runs every ~50 ms would skip entire timeout windows. The tick source is a member (`Gea2TickSource gea2_tick_source_`, `gea2_tick_count_`, `gea2_last_ms_`) — not a file-scope global — so it resets on re-init (deep sleep wake, ESPHome reconfiguration) and two stacks never alias one counter (the property the old `s_gea2_tick_source` global could lose).

**Implementation:** `protocol_stack.cpp` `run_gea2_iteration_()` — the catch-up loop increments `gea2_tick_count_` and publishes `gea2_msec_interrupt_` once per elapsed millisecond (capped at `kMsecCatchupCap` = 1000); `gea2_tick_ticks_()` (the `i_tiny_time_source_t` callback) returns `gea2_tick_count_` when the source's `stack` pointer is non-null, else 0. `init()` wires the source into the GEA2 interface's `tiny_timer_group_init`; `teardown()` nulls the `stack` pointer so the callback reports 0 and re-init starts fresh.

**Verification:** `gea2_tick_source_drives_interface_ack_timeout` (the send → ack-timeout → read-failed sequence advances through the stack's own counter) and `tick_source_is_per_stack_not_shared` (two stacks' GEA2 interfaces time out independently).

#### Requirement 3.2.2: msec Interrupt Scope

The 1 ms `gea2_msec_interrupt_` event MUST be published only inside the GEA2 tight loop — never via a periodic timer in the shared timer group — so it cannot fire in the GEA3 single-pass path or starve the GEA3/polling-bridge timers.

**Rationale:** A 1 ms periodic timer in the shared group would fire ~10× per GEA3 iteration (10 ms) and dominate the timer queue.

**Implementation:** `protocol_stack.cpp` `run_gea2_iteration_()` publishes the event inside the catch-up loop; `init()` only initializes the event when the GEA2 UART is configured.

**Verification:** `init_with_gea3_uart_initializes_gea3_path` and `init_without_uarts_initializes_timer_group_only` confirm the event is inert without a GEA2 UART.

### 3.3 Tight-Loop Bounds

#### Requirement 3.3.1: Duration and Hard Cap

The GEA2 tight loop MUST run for `kGea2LoopDurationMs` (100 ms — the full TX→RX cycle at 19200 baud) with a hard cap at twice that (`kGea2LoopHardCapMs`), logging a warning and breaking if the cap is exceeded. The GEA3 iteration uses 10 ms with the same 2× cap. In test builds (`UNIT_TEST_BUILD`) the loop MUST collapse to a single iteration call.

**Rationale:** The 100 ms busy loop blocks the ESPHome main loop; the cap prevents a stalled `millis()` source from hanging the device. The `esp_task_wdt_reset()` call inside the loop keeps the watchdog fed on ESP32 targets.

**Implementation:** `protocol_stack.h` `run_tight_loop_()` (template, inlined); constants `kGea2LoopDurationMs`, `kGea2LoopHardCapMs`, `kGea3LoopDurationMs`, `kGea3LoopHardCapMs`.

**Verification:** Compile-time: the test build defines `UNIT_TEST_BUILD`, so the single-pass branch is exercised by every stack test; the bound logic is firmware-only and verified by the `Long protocol stack run` warning path in production logs.

### 3.4 Re-Init Safety

#### Requirement 3.4.1: teardown() Resets Per-Run State

`teardown()` MUST reset the tick source's `stack` pointer, the tick count, `gea2_last_ms_`, and the GEA2-active latch, so a subsequent `init()` starts from a clean slate.

**Rationale:** ESPHome re-setup (deep sleep wake, YAML reconfiguration) re-runs `setup()` → `init()`; stale tick state would make the first GEA2 iteration publish a burst of catch-up interrupts for time that never elapsed.

**Implementation:** `protocol_stack.cpp` `teardown()`.

**Verification:** `teardown_resets_gea2_active_latch` and the `gea2_last_ms_ == 0` sentinel re-latch asserted by `gea2_tick_source_drives_interface_ack_timeout` (the first `run()` latches at t=1).

### 3.5 Build-Variant Buffer Gating

#### Requirement 3.5.1: An Unconfigured Protocol's Ring Buffers Are Not Declared

`__init__.py` MUST emit `GEA_BRIDGE_HAS_GEA3` when `gea3_uart_id` is configured and `GEA_BRIDGE_HAS_GEA2` when `gea2_uart_id` is configured. Each protocol's three ring buffers, its ERD client and its `tiny_gea*_erd_client_configuration_t` constant, and for GEA2 the GEA2→GEA3 adapter, MUST be declared only under the matching define, and nothing outside that `#ifdef` may reference them. A build with neither define declares both stacks.

**Rationale:** the buffers are the largest static cost of the stack — 5351 B for GEA3 (255 + 1000 + 4096) and 8447 B for GEA2 (255 + 4096 + 4096) — and a single-UART device can never touch the other protocol's. Measured on `docs/test-compile-gea3-only.yaml` (esp32c3, ESP-IDF) with the buffers alone gated: 156 990 B → 148 542 B RAM and 1 476 284 B → 1 472 036 B flash. Gating the ERD clients and the adapter as well removes a few hundred bytes more per single-protocol build; that was not re-measured on device. A build with neither define is the host test build, which runs no codegen; keeping it "both stacks" means the suite still covers both paths.

**Implementation:** the normalization block at the top of `protocol_stack.h`, the `#ifdef` around the buffer members, and the `#ifdef` around the two `init()` blocks in `protocol_stack.cpp`. The same `#ifdef` also covers each protocol's ERD client, the GEA2→GEA3 adapter, and the two `tiny_gea*_erd_client_configuration_t` constants in `protocol_stack.cpp`: every reference to them is already inside the matching gate, so a single-protocol build would otherwise declare them unreferenced, which clang rejects under `-Werror` (`-Wunused-private-field`, `-Wunused-const-variable`). The five protocol-specific accessors carry variant handling (requirement 3.5.2). The interface, UART adapter and tick-source members stay declared because `run()` and `teardown()` reference them without a gate, so the orchestration layer needs no variant handling.

**Verification:** `make check-protocol-variants` compiles every component source with `-DGEA_BRIDGE_HAS_GEA3` and with `-DGEA_BRIDGE_HAS_GEA2` alone; removing the guard around a buffer reference fails it. `test.yml` runs it twice, with GCC and with clang: only clang emits `-Wunused-private-field` and the C++ form of `-Wunused-const-variable`, so the clang run is what catches a gated member left declared outside its `#ifdef`. `scripts/check_protocol_buffer_defines.py` asserts the defines track the configured UARTs in the generated builds of `docs/test-compile.yaml`, `docs/test-compile-gea3-only.yaml` and `docs/test-compile-gea2-only.yaml` (run by `esphome-compile.yml`). `protocol_stack.h` includes `esphome/core/defines.h` directly, so the on-device define does not depend on an indirect include chain.

#### Requirement 3.5.2: The Runtime Gate Follows the Compile-Time Gate

When `GEA_BRIDGE_HAS_GEA3` is absent, `init()` MUST set `gea3_uart_` to null regardless of the argument (likewise for GEA2), so `has_gea3_uart()` can never report a protocol whose buffers were compiled out.

**Rationale:** the coupling between "define emitted" and "UART configured" lives in codegen, not in this file. Pinning the pointer locally turns a divergence into a disabled protocol instead of a GEA2 write into a non-existent 4096-byte ring.

**Implementation:** the `#ifdef`/`#else` around the two pointer assignments at the top of `init()`.

**Accessor contract (rec 9 review):** the protocol-specific accessors (`get_gea3_client()`, `get_gea2_client()`, `get_gea2_adapter_client()`, `get_gea3_uart_adapter()`, `get_gea2_uart_adapter()`) return `nullptr` when the matching define is absent. A caller that skips the `has_gea*_uart()` guard then fails loudly (null deref) instead of silently operating on a zeroed client or adapter that no code path initialized. The host test build defines both, so the suite keeps the unguarded-shape coverage.

---

## 4. Notes

1. **`run(bool gea2_pending)` deviation from the old `run_protocol_stack_()`.** The old bridge method consulted `gea2_protocol_active_` (a bridge member) and the autodiscovery manager directly. The stack cannot reach either, so the bridge computes `gea2_pending` and hands the decision in; the stack ORs in its own latch (`set_gea2_active`). The bridge-side `gea2_protocol_active_` flag was deleted — `initialize_mqtt_client_()` now calls `protocol_stack_.set_gea2_active(true)` for the manual-`device_id`-GEA2-only case.
2. **The shared timer group moved into the stack.** Every data-path component (bridges, managers, the ERD clients) registers timers on the same `tiny_timer_group_t`; the stack's iterations run it. Callers obtain it via `timer_group()` — the bridge no longer owns a `timer_group_` member.
3. **ODR/mocking note.** The unit tests use `StackTestUart` (a `uart::UARTComponent` double local to `protocol_stack_test.cpp`); the startup integration suite uses `LifecycleMockUart`. The two names are distinct because both translation units link into the same test binary — a shared name would violate ODR.
4. **Buffers live in the stack, gated by the configured UARTs.** The GEA3/GEA2 receive buffers (255 B), send queues (1000 B / 4096 B), and client queue buffers (4096 B) moved from bridge members into stack members, shrinking `GeappliancesBridge` by ~9 KB of embedded state and keeping packet memory next to the code that formats it. A protocol whose UART is not configured does not declare them at all (requirement 3.5.1).
