# MQTT Data Publishing Specification

This document defines requirements for MQTT data publishing in the GE Appliances bridge.

## Specification 1: Hexadecimal ERD Data Format

### Requirement

All ERD data published to MQTT topics under `geappliances/<deviceId>/erd/0x<ERD>/value` must be in hexadecimal format.

### Rationale

- Raw binary data is not MQTT-safe; hex encoding ensures reliable transport
- String conversion belongs at the consumer level, not the transport level
- Consistent format simplifies downstream processing and testing

### Implementation

The ERD cache MQTT publisher (`erd_cache_mqtt_publisher.cpp`) converts all ERD values to lowercase hex strings before publishing.

### Example

| ERD | Raw Bytes | Published Payload |
|-----|-----------|-------------------|
| 0x0001 (Model Number) | `4a 45 53 39 35 30 30 53 53 53 00 00` | `4a4553393530305353530000` |
| 0x0002 (Serial Number) | `41 56 56 4c 32 34 44 4d 58 58 41 4b 31 00` | `4156564c3234444d5858414b3100` |

### Prohibited

- Publishing raw binary bytes directly to MQTT
- Converting ERD data to ASCII strings at the MQTT adapter level
- Maintaining a list of "string-type" ERDs for special handling in the MQTT adapter

### Verification

Tests must verify that published payloads are valid hex strings (only characters `0-9a-f`).

## Specification 2: Conditional Full Re-publish After Long MQTT Disconnect

### Requirement

After an MQTT disconnect/reconnect, the publisher checks how long the disconnect lasted. If the disconnect duration is **60 seconds or longer**, it calls `erd_cache_mark_all_updated()` to re-mark every cached ERD for re-publish (a full cache flush). If the disconnect was shorter than 60 seconds, it resumes publishing only entries whose `update_required` flag is `true`.

The disconnect start time is recorded in `disconnect_start_ms` when `on_disconnected()` fires. On reconnect, `on_connected()` computes the elapsed time and compares it against `RECONNECT_REPUBLISH_THRESHOLD_MS` (60000 ms).

### Rationale

- MQTT messages are published with `retain = true`. On client reconnect to a running broker, retained messages persist — Home Assistant re-subscribes and receives them automatically.
- A full re-publish after every reconnect would cause a burst of MQTT traffic proportional to the number of cached ERDs (potentially 200+), unnecessarily loading the broker and network.
- The only scenario where retained messages are lost is a **broker restart** (not a client reconnect). A broker restart typically causes a disconnect lasting 60+ seconds.
- The 60-second threshold (`RECONNECT_REPUBLISH_THRESHOLD_MS`) distinguishes between short network blips (no full re-publish needed — retained messages are intact) and long outages like broker restarts (full re-publish needed — retained store may have been lost).
- This balances correctness (mitigating stale data after broker restarts) with efficiency (avoiding unnecessary bursts on transient disconnects).

### Design Decision

The `on_connected()` callback sets `mqtt_connected = true`, resets `first_round_done` to `false`, and signals the background task. It checks the disconnect duration against the 60-second threshold. When the threshold is exceeded, `erd_cache_mark_all_updated()` is called to trigger a full re-publish. The `on_disconnected()` callback sets `mqtt_connected = false` and records `disconnect_start_ms` via the configured time function (`get_time_ms`, defaulting to `esphome::millis`).

The re-publish it triggers is a burst: every cached entry becomes dirty at once, and the publisher emits one entry per wake. `erd_cache_mqtt_publisher_signal_work()` rate-limits those wakes while the backlog is large, so the burst drains at roughly 10 entries per second instead of at main-loop rate and does not overflow ESPHome's 30-slot outbound MQTT queue (see the publisher spec, §2.5). This does not change *what* is re-published, only how fast; because `update_required` is a flag and each snapshot copies the newest bytes, a slower drain delivers fresher values rather than fewer of them.

### Verification

- Tests verify that after a simulated disconnect/reconnect cycle with a short gap (&lt; 60 s), only entries with new data changes are published — not a full cache flush.
- Tests verify that after a simulated disconnect/reconnect cycle with a long gap (≥ 60 s), `erd_cache_mark_all_updated()` is called and all cached ERDs are re-published.
- Tests verify that `disconnect_start_ms` is recorded on disconnect and cleared on reconnect.
