# Glossary

The shared language for this codebase. Use these terms in code, specs, issues, PRs, and ADRs. When a term here and the code disagree, the code wins — fix this file.

Maintained by the `grill-with-docs` and `domain-modeling` skills: when a grilling session settles terminology, update this file in the same change.

## Protocol

- **ERD** — Electronic Record Descriptor. A 2-byte (`uint16_t`) identifier for a data record on the appliance bus. Printed as 4 hex digits (`0x%04x`) in MQTT topics and logs.
- **GEA2 / GEA3** — The two generations of GE's serial appliance protocol. GEA2 runs at 19200 baud and has no subscriptions (appliances are forced into polling mode); GEA3 runs at 230400 baud and supports ERD subscriptions.
- **Board address** — A 1-byte physical address of a board on the GEA bus, `0x00`–`0xFE`. `0xFF` is the broadcast address and can never be a physical board address.
- **Primary board** — The appliance's main board. Its detected physical address is stored as `primary_board_address`. In the write path and the polling list, `0xFF` is used as a sentinel meaning "the primary board" (it can never collide with a physical address).
- **Secondary board** — Any board other than the primary. Its ERDs get the `0x{addr}_` topic prefix.
- **Host address** — The appliance's board address, resolved during discovery.
  Defaults to `0` until autodiscovery completes; the write bridge defensively
  rejects writes when its host address is the broadcast address `0xFF` (which
  can never be a physical address).
- **Probe entry** — One item in the polling list. `board_address` of `0xFF` (`PROBE_ENTRY_DEFAULT_ADDRESS`) means "use the host address".
- **Matched pair / paired ERD** — A request/status ERD pair (e.g. request `0x7708` and its status ERD). The `paired_erd` override links them manually when auto-detection fails.
- **RMW (read-modify-write)** — The write strategy for matched-pair request ERDs: read the record, modify the target field, write the whole record back.
- **Feature bit / feature list** — Bitmasks in feature ERDs (common `0x0092`, appliance-specific `0x0093`–`0x0097`, `0x0109`–`0x010D`) that determine which ERDs an appliance exposes. Parsed across multiple timer callbacks to stay under the task watchdog.
- **Appliance type** — Identified from identity ERDs at startup; selects the feature list and ERD list via `appliance_type_map.h`.
- **Device ID** — The string naming the bridge instance in MQTT topics. Auto-generated from identity ERDs or set in YAML.

## MQTT

- **Value topic** — `geappliances/{device_id}/erd/0x{ERD}/value` (primary board) or `geappliances/{device_id}/erd/0x{ADDR}_0x{ERD}/value` (secondary).
- **Write topic** — Same shape with `/write`; the bridge subscribes and forwards to the appliance.
- **Write result topic** — Same shape with `/write_result`.
- **Legacy no-prefix topic** — The primary-board form without `0x{ADDR}_`. Single-board appliances must keep working on this format.
- **Availability topic** — The MQTT availability (LWT) topic; marks all appliance entities unavailable when the bridge goes offline. Included in the discovery data hash.
- **command_topic** — The topic Home Assistant writes to for controllable entities (numbers, switches).
- **Demotion** — Downgrading a partial-write ERD to a read-only sensor entity at discovery time (issue #156): the appliance accepts the write but cannot confirm the full record, so the entity must not claim to be controllable.

## Data path

- **ERD cache** — The decoupling boundary between the appliance-facing and MQTT-facing halves. A fixed 300-slot array keyed by `(erd, board_address)`, backed by a 4096-byte bump arena, guarded by a single mutex. Entries are never removed; data is updated in place.
- **Bump arena** — The 4 KB static region holding all ERD payloads. Each entry claims a contiguous slice via a bump pointer at registration; updates are in-place `memcpy`. Never shrinks.
- **update_required** — Per-entry flag set when a value changes; the publisher drains flagged entries round-robin.
- **Snapshot publish** — The publisher atomically copies one payload out of the arena under the cache lock, then publishes without holding the lock. The cache mutex is never held across the slow MQTT publish.
- **Publisher** — `erd_cache_mqtt_publisher`. A FreeRTOS task pinned to Core 0 at priority 1 (below the main loop), publishing one entry per loop iteration. Pauses on MQTT disconnect; forces a full republish after a disconnect longer than 60 seconds.
- **Single-flight** — The write bridge admits one write at a time (two-state HSM: ready/writing). New write requests arriving mid-write are dropped by design.
- **Request ID** — Tracked per write so out-of-order completions and stale responses can be matched or discarded.

## Bridges and modes

- **Bridge mode** — POLL, SUBSCRIBE, or AUTO, selected at initialization. GEA2 appliances are forced to POLL. AUTO tries subscription first and falls back to polling on a 2-second publication quiet period or three consecutive subscription request failures.
- **Subscription bridge** — Registers ERD subscriptions and routes appliance publications into the cache.
- **Polling bridge** — Periodically reads the polling list; budgets MQTT work per loop tick.
- **Write bridge** — Forwards MQTT write commands to the appliance; gated on host-address resolution; single-flight.
- **Startup HSM** — The 8-state startup state machine (representative phases:
  autodiscovery → identity → feature bits → mode selection → bridge initialization).
- **IBridgeServices** — The abstract C++ interface decoupling the startup HSM from the concrete `GeappliancesBridge`.
- **Tick-counter time source** — GEA2's millisecond timer: a counter incremented once per real millisecond inside the GEA2 tight loop, catching up missed milliseconds via `msec_interrupt` events.

## HA discovery

- **Pipeline** — `scripts/ha_discovery/run_pipeline.py`. Auto-detects HA domain, device class, unit, and scaling from the public appliance API ERD definitions, applies overrides, and generates the discovery artifacts.
- **Override** — A manual correction in the `OVERRIDES` dict in `scripts/ha_discovery/pipeline/post_process.py`. Applied after auto-detection so it survives pipeline reruns. Keys are bare `erd_id` or `erd_id:offset`.
- **Generated files** — `erd_lists.{h,cpp}`, `appliance_api_feature_lists.{h,cpp}`, `appliance_type_map.h`, `ha_discovery_data.{h,cpp}`, `ha_discovery/*.jsonl`, and `appliance_api_erd_definitions_processed.json`. Produced by scripts only; never hand-edited; CI enforces sync via `scripts/check_generated_sync.sh` (`make check-generated`).
- **Discovery data** — The zlib-compressed HA discovery payloads embedded in `ha_discovery_data.h`. Decompressed at startup into pre-allocated buffers.
- **Discovery data hash** — Hash of the discovery payloads plus the availability topic. Discovery republishes when the hash or device ID changes, or when the Discovery Refresh button is pressed; normal boots skip it (topics are retained).
- **Discovery Refresh button** — A button entity that forces a discovery republish.
- **Scaling factor** — The divisor applied to raw ERD values to produce engineering units.

## Testing and factory

- **Test double** — A CppUTest mock standing in for the GEA3 ERD client, MQTT client, or timer group.
- **Simulation test** — An application-level test (`test/simulation/`) that drives complete workflows (startup, mode fallback, write forwarding) through the doubles without hardware.
- **Watchdog budgeting** — Splitting long operations across timer ticks or loop iterations so the ESP32 task watchdog never fires.
- **Test bench** — The physical LILYGO T-SIM7670G-S3 (ESP32-S3 + SIM7670G LTE + Tailscale) used for device verification: OTA upload, HA-sensor checks, MQTT observation, log capture. See the `device-verify` skill for the loop.
- **Device verification** — The factory stage that proves a firmware-facing change on the test bench: build → OTA → HA online check → MQTT scenario → log capture → benchmark → evidence posted to the issue.
- **Canary metrics** — The five performance metrics watched for regressions on every firmware build: loop time, min heap, ERDs in cache, ERD publish rate, MQTT publish rate. Exposed as HA sensor entities (ESPHome `debug` platform for loop time and min heap; `DiagnosticSensorPublisher` for the cache and publish rates) and benchmarked against the `develop` baseline in device verification.
- **Formal model** — The TLA+ model of the concurrency core (ERD cache, MQTT publisher, write bridge, startup HSM) in `formal/`, checked by TLC in CI (ADR-0003). It models the protocol, not the C: memory safety, real-time, and performance are out of scope. Model and spec commit together.
- **Bug path** — The shortened factory path for bugs: triage → diagnose → implement → review → device verify → PR, with grill/spec/tickets skippable unless the fix is a design decision.
