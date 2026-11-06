# Architecture Review

A review of `components/geappliances_bridge/` as of October 2026 (branch `develop`, ~413 commits),
with prioritized recommendations. It covers the firmware component only; the Python pipeline under
`scripts/ha_discovery/` is out of scope except where it generates firmware tables.

This document complements [overview.md](./overview.md), which describes the architecture as built.
Recommendations here are proposals, not commitments. Each one names the files involved, the
expected benefit, and the risk, so they can be scheduled independently.

## Summary

The component is in good shape for an embedded bridge of this complexity. Its strongest points:

- **Layering is explicit and mostly honored.** C structs with vtables carry the data path
  (`erd_cache`, the three bridges, the publisher, discovery); C++ classes wrap ESPHome. The
  `i_mqtt_client_t`, `i_tiny_uart_t`, and `IBridgeServices` interfaces give every module a test
  seam, and 537 CppUTest cases plus a simulation harness exercise them.
- **Concurrency is contained.** Only two things run off the main loop: the MQTT publisher task and
  the ESP-IDF MQTT callback used by discovery cleanup. Each has one clearly documented lock, and the
  snapshot-then-publish pattern keeps the cache lock off the slow path.
- **Memory is bounded.** No dynamic containers; every list has a fixed capacity derived from the
  generated ERD tables. The 44 KB of discovery buffers are heap-allocated only during a run.
- **Generated data is kept honest.** CI fails if `erd_lists`, `appliance_api_feature_lists`,
  `appliance_type_map`, or `ha_discovery_data` drift from their generators.

The weaknesses are the ones that accumulate in a codebase that grew by fixing field issues one at a
time: a central class that still does too much, state tracked as loose booleans in three places,
knowledge duplicated between the generator and hand-written C, and a main-loop model that trades
responsiveness of every other ESPHome component for protocol timing.

The table below ranks the recommendations. "Effort" is a rough engineering estimate; "Risk" is the
chance of a behavioral regression on real hardware.

| # | Recommendation | Benefit | Effort | Risk |
|---|---|---|---|---|
| 1 | Extract a `ProtocolStack` from `GeappliancesBridge` | Readability, testability, removes a global | Medium | Low |
| 2 | Replace boolean flag clusters with explicit state enums | Fewer invalid states, easier debugging | Medium | Low |
| 3 | Pick one startup HSM driving model (signals or polling, not both) | Simpler HSM, fewer redundant checks | Low | Low |
| 4 | Centralize MQTT topic formatting and parsing | Removes 4 copies of the topic grammar | Low | Low |
| 5 | Generate the appliance-type to category map | Removes a hand-maintained 56-entry table | Low | Low |
| 6 | Inject time and yield into the data-path C modules | Restores the "portable C" promise, enables deterministic tests | Low | Low |
| 7 | Remove dead and test-only API surface | Smaller contract to maintain | Low | None |
| 8 | Replace `std::function` so integration tests can run in CI | 97 tests move from local-only to CI | Low | Low |
| 9 | Size protocol buffers from configuration | Reclaims up to ~14 KB RAM on single-UART builds | Medium | Low |
| 10 | Move the `tinfl` decompressor state out of the permanent object | Reclaims ~8 KB RAM | Low | Low |
| 11 | Queue writes instead of dropping a second in-flight write | Fixes a user-visible HA behavior | Medium | Medium |
| 12 | Reconsider the post-discovery reboot | Faster first boot, less NVS churn | Medium | Medium |
| 13 | Move the protocol tight loop off the ESPHome main loop | Frees 10-100 ms per loop for other components | High | High |
| 14 | Fix stale architecture docs | Correct onboarding | Low | None |

## Structure

### 1. Extract a `ProtocolStack` from `GeappliancesBridge`

`GeappliancesBridge` (`geappliances_bridge.h`, `geappliances_bridge.cpp`,
`geappliances_bridge_bridge_init.cpp`) is still a god object despite the manager extractions already
done. The header declares roughly 70 data members. They fall into four groups that have no reason to
share a class:

| Group | Members | Natural home |
|---|---|---|
| ESPHome glue | `uart_`, `gea2_uart_`, sensors, setters, `setup()`, `loop()`, `dump_config()` | Stays in `GeappliancesBridge` |
| Protocol stack | both UART adapters, `gea3_interface_`, `gea2_interface_`, both ERD clients, the GEA2 adapter, all six buffers, the tick source, `run_*_iteration_()`, `run_tight_loop_()`, `run_protocol_stack_()` | New `ProtocolStack` |
| Data plane orchestration | the three bridges, `init_polling_bridge_()`, custom-ERD polling, mode fallback, `check_steady_state()`, the `*_initialized_` flags | New `BridgeOrchestrator` (or fold into the startup HSM services) |
| Startup services | the 24 `IBridgeServices` overrides | Thin forwarding to the two classes above |

The protocol stack is the cleanest cut. It has a tiny surface: construct from zero, one, or two UARTs;
expose `get_gea3_client()`, `get_gea2_client()`, `get_gea2_adapter_client()`, the two UART adapters,
`set_gea2_active(bool)`, and `run()`. Extracting it also removes the file-scope global
`s_gea2_tick_source` in `geappliances_bridge.cpp`, which exists only because the tick callback cannot
reach a class member; a `ProtocolStack` instance can own the tick source struct directly. The
`friend tiny_time_source_ticks_t gea2_tick_ticks(...)` declaration goes with it.

The orchestration cut is less clean because the startup HSM's `IBridgeServices` interface already
mirrors these internals one-to-one. Doing recommendation 3 first shrinks that interface and makes the
second extraction smaller.

### 2. Replace boolean flag clusters with explicit state enums

Three places track lifecycle with independent booleans that are only valid in a handful of
combinations:

- `GeappliancesBridge`: `mqtt_client_adapter_initialized_`, `erd_bridge_initialized_`,
  `subscription_bridge_initialized_`, `polling_bridge_initialized_`, `write_bridge_initialized_`,
  `custom_erd_polling_started_`, `feature_bit_reading_started_`, `steady_state_reached_`,
  `gea2_protocol_active_`, `erd_cache_publisher_paused_`, `discovery_just_resumed_`.
- `OtaCleanupManager` (`ota_cleanup_manager.h`): `ota_cleanup_needed_`, `ota_cleanup_in_progress_`,
  `ota_discovery_publishing_`, `ota_reboot_pending_`, `discovery_refresh_in_progress_`,
  `initial_discovery_needed_`, `initial_discovery_done_`, plus a `CleanupTrigger` enum. Every entry
  point starts with a six-way guard checking that none of the others is set.
- `erd_bridge_poll_t` (`erd_bridge_poll.h`): `polling_list_complete`, `polling_timer_armed`,
  `restart_pending`, `cycle_sending_in_progress`, `cycle_has_failure`, alongside the HSM's own
  `current_state`.

The `OtaCleanupManager` case is the clearest. Its `loop()` is already a hand-rolled state machine
over the flags; the states are `Idle`, `WaitingForReady`, `Cleaning`, `Publishing`, `RebootPending`,
with a separate `trigger` field for why. Rewriting it as an enum plus a `switch` removes the guard
boilerplate and makes the one-line log of "what is discovery doing" trivial. The project already has
`tiny_hsm` for anything that needs entry/exit actions; a plain enum is enough here.

While there, rename the class. It owns initial discovery, refresh-button discovery, and OTA-triggered
discovery; `DiscoveryLifecycleManager` or `DiscoveryCoordinator` describes it. The class also receives
`bool&` references to two `GeappliancesBridge` members (`steady_state_reached_`,
`mqtt_client_adapter_initialized_`). Pass a small `is_ready()` callback or a reference to the
orchestrator from recommendation 1 instead of aliasing another object's fields.

### 3. Pick one startup HSM driving model

`geappliances_bridge_startup_hsm.cpp` is driven two ways at once. Every state handles
`signal_run_loop` by polling an `is_*_complete()` service method, and most states also handle an
explicit completion signal (`signal_autodiscovery_complete`, `signal_device_id_complete`,
`signal_feature_bits_complete`, `signal_bridge_ready`) that performs the same transition. Some
signals are declared and never sent by production code: `signal_mqtt_connected` is handled in
`startup_state_feature_bits` but nothing publishes it anywhere, and `signal_feature_bits_complete` is
sent only from `test/tests/startup_hsm_test.cpp`.

The polling path is what actually drives the machine today: `loop()` sends `signal_run_loop` on every
iteration, and all transitions are reachable from it. The event path only adds latency savings of one
loop iteration. Choose one:

- **Keep polling, drop the events.** Delete the unused signals and the `tiny_event`-to-HSM bridging
  in `setup()` and `handle_erd_client_activity_()`. The HSM becomes a pure "check conditions each
  tick" sequencer, which is easy to reason about and already what the tests exercise.
- **Keep events, drop polling.** Each manager publishes completion once; `signal_run_loop` shrinks to
  the states that genuinely need periodic work (`startup_state_running`). This is the better long-term
  design if recommendation 13 is pursued, because the HSM would no longer be tied to the main loop.

Either way, every state currently has `startup_state_top` as its parent, so the machine is flat.
That is fine, but the "hierarchical" label in the docs over-promises; a note in the spec would help.

### 4. Centralize MQTT topic formatting and parsing

The topic grammar from the Global Protocol Rules
(`geappliances/{device_id}/erd/0x{ERD}/{op}` and `geappliances/{device_id}/erd/0x{ADDR}_0x{ERD}/{op}`)
is implemented independently in four places:

| File | Function | Direction |
|---|---|---|
| `erd_cache_mqtt_publisher.cpp` | `mqtt_publisher_task()` and `erd_cache_mqtt_publisher_loop()` | format `value` |
| `esphome_mqtt_client_adapter.cpp` | `update_erd_write_result()` | format `write_result` |
| `esphome_mqtt_client_adapter.cpp` | write-topic subscribe lambda | parse `write` |
| `ha_discovery_manager.cpp` | `format_erd_topic()` | format `value` and `write` for discovery payloads |

Each uses a different primary-board sentinel convention (the publisher compares against the detected
address, the write path uses `0xFF`, discovery uses an empty string). A single header, for example
`erd_topic.h`, with `erd_topic_format(buf, size, device_id, addr_or_primary, erd, op)` and
`erd_topic_parse(topic, &addr, &erd)` would make the grammar one definition and one test file. The
write-result path should also go through `i_mqtt_client_t::publish_raw` rather than reaching for
`esphome::mqtt::global_mqtt_client` directly, which it does today.

### 5. Generate the appliance-type to category map

`should_process_category()` in `ha_discovery_manager.cpp` is a 100-line hand-written function mapping
appliance types from ERD 0x0008 onto the nine JSONL categories. The same type list is already
generated into `appliance_type_map.h`, and `erd_lists.cpp` already generates
`applianceTypeToErdGroupTranslation[]` indexed by appliance type. When GE adds a type, the generator
picks it up and this function silently does not. That has already happened: the generated map knows
types 0 through 58, while `should_process_category()` stops at 56, so an AirPurifier (57) or
HomeVentilation (58) appliance receives only the common entities today.

Add the category name to the generator's output, either as a third generated table
(`appliance_type_to_discovery_category[]`) or as a field on the existing translation struct, and
reduce `should_process_category()` to a lookup plus the "common and energy apply to everyone" rule.

Related: `appliance_type_to_string()` in the generated `appliance_type_map.h` returns `std::string`,
which heap-allocates on every call. It is called once per boot in `DeviceIdentityManager`, so this is
not a performance problem, but returning `const char*` matches the rest of the component's
no-allocation posture and costs nothing in the generator.

### 6. Inject time and yield into the data-path C modules

The header convention in `geappliances_bridge.h` promises "C structs + vtables: portable, testable,
no C++ overhead" for the data path. `erd_bridge_poll.cpp` calls `esphome::millis()` in six places and
`esphome::delay(0)` once; `erd_cache_mqtt_publisher.cpp` defaults `get_time_ms` to `esphome::millis`
and reads `esphome::mqtt::global_mqtt_client` at init. The publisher already has the right pattern
(`erd_cache_mqtt_publisher_set_time_fn`); the polling bridge should take the same `get_time_ms`
function pointer through `erd_bridge_poll_init()`, and the single `delay(0)` should become a
`yield` callback or be removed in favor of returning early (the surrounding budget logic already
returns to the main loop).

This matters for tests more than for portability: the polling bridge's cycle-timing logic
(`restart_pending`, `cycle_sending_in_progress`, the 100 ms send budget) is currently tested against
the real host clock through the `esphome_hal_double`, which makes those tests timing-sensitive.

### 7. Remove dead and test-only API surface

Several public functions have no production caller:

| Symbol | File | Status |
|---|---|---|
| `erd_cache_mqtt_publisher_loop()` | `erd_cache_mqtt_publisher.cpp` | Synchronous publish path; only tests call it. Duplicates the task's topic and hex formatting. Either delete it and port the four tests to drive the task body, or extract a shared `publish_one()` that both paths call. |
| `erd_cache_get_next_updated()` | `erd_cache.cpp` | Comment says "declared for future use". Only one test uses it. |
| `ErdRegistry::register_erd()`, `registered_erd_count()`, `registered_erd()`, `clear_registered_erds()` | `erd_registry.cpp` | The "registered ERD" set is written only by tests; nothing in the component reads it. |
| `esphome_mqtt_client_adapter_set_erd_registry()` | `esphome_mqtt_client_adapter.cpp` | Stores the pointer; nothing in the adapter reads it. The bridge calls it at init. |
| `erd_write_bridge_set_host_address()` | `erd_write_bridge.cpp` | Test-only. |
| `AutodiscoveryManager::feed_byte()` | `autodiscovery_manager.h` | Test-only; harmless but should be marked as such. |

Deleting these also shrinks the `ErdRegistry` to what it is: a sorted valid-ERD set with
`set_valid_erds`, `add_valid_erds`, and `is_valid`. The subscription bridge then no longer needs to
carry it as `void*`; a forward-declared struct or a tiny C interface (`bool (*is_valid)(void*, erd)`)
keeps the bridge free of the C++ class.

### 8. Replace `std::function` so integration tests can run in CI

`.github/copilot-instructions.md` notes that 97 startup integration tests are excluded from CI because
CppUTest's memory checker reports false corruption with `std::function` members. Only two members use
it: `AutodiscoveryManager::on_complete_cb_` and `OtaCleanupManager::reboot_callback_`. Both are
non-capturing or capture only `this`. The rest of the component uses the `tiny` convention of a
function pointer plus `void* context`. Converting these two restores the convention and should let
`make integration-test` join the `test` job in `.github/workflows/test.yml`.

## Memory

### 9. Size protocol buffers from configuration

`GeappliancesBridge` embeds both protocol stacks unconditionally. Measured from the declared array
sizes, the bridge object carries roughly 60 KB of static storage; the table shows the large items.

| Member group | Approximate size | Notes |
|---|---|---|
| GEA2 buffers (`gea2_send_queue_buffer_`, `gea2_client_queue_buffer_`, receive) | 8.4 KB | Reserved even when only `gea3_uart_id` is configured |
| GEA3 buffers (`send_queue_buffer_`, `client_queue_buffer_`, receive) | 5.3 KB | Reserved even when only `gea2_uart_id` is configured |
| `erd_cache_t` | 7.7 KB | Needed |
| `ha_discovery_manager_t` including `tinfl_decompressor` and cleanup `topic_buf` | ~18 KB | See recommendation 10 |
| `erd_cache_mqtt_publisher_t` (task stack 4 KB, hex buffer 512 B) | 4.9 KB | Needed |
| `poll_probe_list_` plus `erd_bridge_poll_t` lists and set | 6.1 KB | Sized for 659 ERDs |
| `ErdRegistry` plus `FeatureBitManager` valid-ERD arrays | 4.0 KB | Two copies of the same 645-entry set |

Two low-risk changes:

- The Python layer in `__init__.py` knows which UARTs are configured. Emit `cg.add_define(...)` for
  `GEA_BRIDGE_HAS_GEA3` and `GEA_BRIDGE_HAS_GEA2` and guard the buffer declarations. Recommendation 1
  makes this natural: `ProtocolStack` can hold a `union` or conditional members.
- `FeatureBitManager::valid_erds_` and `ErdRegistry::valid_erds_` hold the same 645-entry array.
  `ErdRegistry::set_valid_erds()` copies and sorts; the manager could sort in place and the registry
  could borrow the pointer, or the manager could write directly into the registry.

### 10. Move the `tinfl` decompressor state out of the permanent object

`ha_discovery_manager_t` embeds `tinfl_decompressor decomp_state` directly. That struct is about
8 KB (the fast-lookup tables alone are 3 x 1024 x 2 bytes). The three large discovery buffers were already moved to
heap-during-run; the decompressor state should follow, allocated in `ha_discovery_manager_start()`
and freed in `cleanup_resources()`. The 6 KB `ha_discovery_cleanup_t::topic_buf` is also only used
during a cleanup pass and could join it, though the per-instance spinlock embedded in that struct
means the allocation boundary needs care.

## Behavior

### 11. Queue writes instead of dropping a second in-flight write

`erd_write_bridge.cpp` holds exactly one pending write. A second `signal_write_requested` while in
`state_writing` is logged and dropped, and no `write_result` is published for it. Home Assistant
automations routinely issue two MQTT commands back to back (set a mode, then a setpoint), and the GEA
client already has a request queue underneath. A small fixed-capacity ring of pending
`(erd, board_address, size, value[248])` entries, drained one at a time, would honor the existing
single-outstanding-request design while not losing commands. Capacity of 4 costs about 1 KB. The
dropped-write case should also publish a `write_result` error so HA is not left waiting.

### 12. Reconsider the post-discovery reboot

`OtaCleanupManager::loop()` schedules a reboot five seconds after every successful discovery
publish, including the very first boot on a new install. The code comments trace this to a time when
the discovery buffers lived in the object and fragmented the heap. They are now heap-allocated per
run and freed, and the `ha_discovery_manager_init()` reset path already handles the failure case
without rebooting.

Removing the reboot would cut first-boot time, avoid an extra NVS write cycle, and remove the
`SAFE_MODE_RTC_KEY` and `esp_ota_mark_app_valid_cancel_rollback()` coupling from a discovery module
where it does not belong (ESPHome's OTA component already handles rollback confirmation). The risk is
heap fragmentation on long-running single-core devices after a refresh-button cycle; measure
`heap_caps_get_largest_free_block()` after a few refresh cycles on a C3 before committing to it.

### 13. Move the protocol tight loop off the ESPHome main loop

This is the largest and riskiest item and is listed for completeness rather than as a near-term plan.

`run_protocol_stack_()` busy-waits inside every `loop()` call: 10 ms for GEA3, 100 ms for GEA2
(hard-capped at 2x). The reason is sound: the UART adapter polls bytes from a timer, and the
appliance's response window at 19200 baud is too short to span the ~50 ms gap between ESPHome loop
iterations. The cost is that every other component on the device, and ESPHome's own API and OTA
handling, is held off for that long on every iteration. The `esp_task_wdt_reset()` calls sprinkled
through `loop()` are a symptom.

The publisher already shows the alternative: a dedicated FreeRTOS task. A `ProtocolStack` task
(recommendation 1 makes this possible) could run the UART poll, interfaces, and ERD clients
continuously at a modest priority, with the ERD-client activity events marshalled to the main loop
through a FreeRTOS queue. The `tiny` library is single-threaded by design, so the boundary must be
drawn carefully: everything that touches `timer_group_`, the interfaces, or the clients moves to the
task; the cache (already mutex-protected) and MQTT stay on the main loop and the publisher task.
Prerequisites: recommendations 1, 3 (event-driven variant), and 6.

## Documentation

### 14. Fix stale architecture docs

`docs/architecture/overview.md` describes the ERD cache as requiring no locking and says the custom
ERD list caps at 64 entries. Both are out of date: `erd_cache_t` has had a mutex since the publisher
moved to Core 0, and `CUSTOM_ERDS_MAX` is 128. The same section attributes the cleanup critical
section to `vPortEnterCritical`; the code uses `taskENTER_CRITICAL` with a per-instance
`portMUX_TYPE`. The Critical Invariants in `.github/copilot-instructions.md` are correct and should be
treated as the source; `overview.md` should point to them rather than restating.

`docs/README.md` and `docs/style-guide.md` both reference `docs/architecture/roadmap.md`, which does
not exist. This document can serve as its starting point, or the links should be removed.

Smaller drift worth fixing alongside: the `esphome_mqtt_client_adapter_init()` comment says
`device_id` points into "ESPHome's YAML config strings", but it points into
`DeviceIdentityManager::generated_device_id_`; the lifetime is still fine, the reason given is not.

## Suggested Order

The recommendations are independent enough to land as separate pull requests. A reasonable sequence,
cheapest and safest first:

1. Recommendations 7, 8, 14: deletions and documentation. No behavior change; CI gains 97 tests.
2. Recommendations 4, 5, 6: consolidation of duplicated logic and dependency injection. Pure
   refactors with existing test coverage.
3. Recommendation 2 (`OtaCleanupManager` first, then the poll bridge flags).
4. Recommendation 3, then 1. The HSM cleanup shrinks the interface that the extraction has to cut.
5. Recommendations 9 and 10 once `ProtocolStack` exists.
6. Recommendations 11 and 12 after hardware measurement.
7. Recommendation 13 only if the main-loop latency becomes a user-reported problem.
