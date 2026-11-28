# ADR-0018: The ESPHome layer is a thin seam — C structs for the data path, one MQTT adapter, header-only C headers, pinned libraries

Status: accepted (2026-11-27)
Related: ADR-0016, ADR-0019, `docs/style-guide.md`, `docs/reference/interfaces.md`

## Context

The component is simultaneously an ESPHome component and a protocol/data-path
library that must run under CppUTest with stubbed ESP-IDF. Every place those two
worlds touch directly — ESPHome's global MQTT client, C++ classes in the data
path, upstream libraries floating on branch names — is a place where a change in
one world silently breaks the other.

## Decision

- **C structs plus function-pointer vtables for the data path** (`erd_cache`,
  the bridges, the interfaces, the publishers, discovery): portable, testable, no
  C++ overhead. **C++ classes only for the ESPHome integration layer**
  (`GeappliancesBridge`, the managers, the adapters), and only where ESPHome APIs
  or C++ features are actually needed.
- **ESPHome's global MQTT client is reached from exactly one place**:
  `esphome_mqtt_client_adapter`. Data-path modules query connectivity and
  publish through the injected `i_mqtt_client_t`, never around the adapter. The
  adapter stores a pointer into `DeviceIdentityManager`'s buffers
  (`configured_device_id_` or `generated_device_id_`), which live in the bridge
  object and outlive it.
- **MQTT lifecycle edges are handled at the seam.** If the client is already
  connected when callbacks register, the connect event fires immediately so the
  publisher's `mqtt_connected` flag is right on the first `loop()`. `destroy()`
  unregisters every callback, because a dangling lambda capture firing after the
  adapter is gone (re-init, OTA) is a use-after-free.
- **Diagnostic entities are created unless their option is set to `false`, and
  their defaults live in `CONFIG_SCHEMA`** so config validation materializes the
  entity `id` and — because the component depends on `mqtt` — an `mqtt_id`.
  Codegen emits the `MQTTSensorComponent` / `MQTTButton` wrapper that publishes
  an entity's state **only for entities carrying that key**, so an entity
  assembled by hand is registered but never published. CI enforces the pairing:
  the check regenerates the config with `--only-generate` and asserts every
  component-created diagnostic entity has an MQTT wrapper, without needing an
  ESP-IDF build.
- **Library dependencies are pinned to commit SHAs** to prevent silent breakage
  from upstream branch movement. `tiny-gea-api` transitively depends on
  `ryanplusplus/tiny` (^7.1.3); pinning `tiny-gea-api` is sufficient, because
  adding `tiny` directly creates a duplicate dependency-graph node
  (`"ryanplusplus/tiny"` vs `"tiny"`) that triggers a deduplication warning in
  ESPHome 2026.x.
- **Shared C headers are header-only by construction.** `erd_bridge_common.h`
  functions are templates or `inline`, and `geappliances_bridge_constants.h`
  constants are `static constexpr`, so each translation unit gets its own copy
  with no ODR violation. The logging tag is translation-unit-local so it does not
  trip `-Wunused-const-variable` when the ESP log macros are compiled out, and
  values used only by a log macro are explicitly `(void)`-cast so a stubbed
  `ESP_LOGI` does not trip `-Wunused-but-set-variable`.
- **Setup ordering:** the bridge sets up after MQTT and at the same priority as
  the UART adapters, so the clients exist before the bridge wires them.

## Consequences

- The data path stays host-testable: swapping `i_mqtt_client_t` is the only way
  a test reaches MQTT, and no data-path file includes ESPHome headers.
- Adding a diagnostic entity means adding it in `CONFIG_SCHEMA` **and** its MQTT
  wrapper; the CI check fails if the wrapper is missing.
- Bumping a vendored library means changing a SHA, and only one SHA per
  transitive chain.
- New shared-header symbols must be `inline`, `static constexpr`, or templates;
  a file-scope definition in a header breaks the build variants (ADR-0013).

## Code anchors

- `geappliances_bridge.h` — the C/C++ convention block; `get_setup_priority()`.
- `esphome_mqtt_client_adapter.{h,cpp}` — the MQTT seam, callback registration
  and teardown.
- `components/geappliances_bridge/__init__.py` — `CONFIG_SCHEMA` diagnostic
  defaults, library SHA pins, the `GEA_BRIDGE_HAS_*` defines.
- `scripts/check_diagnostic_mqtt_wrappers.py` + `Makefile` — the wrapper gate.
- `erd_bridge_common.h`, `geappliances_bridge_constants.h`,
  `geappliances_bridge_log.h` — header-only and log-tag conventions.
