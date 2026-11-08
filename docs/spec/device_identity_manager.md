# Device Identity Manager — Specification

## 1. Overview

### 1.1 Purpose

The Device Identity Manager reads three identity ERDs from the appliance — appliance type (0x0008), model number (0x0001), and serial number (0x0002) — and assembles a stable, MQTT-topic-safe device identifier string. It is fully self-driving: each callback queues the next read or retries the current one.

### 1.2 Responsibilities

- Read ERDs 0x0008 (appliance type), 0x0001 (model number), 0x0002 (serial number) in fixed sequence
- Retry each ERD read indefinitely on failure — the manager never advances until the current ERD reads successfully
- Sanitize raw model number and serial number values into MQTT-topic-safe strings
- Concatenate the appliance type string, sanitized model, and sanitized serial into a device ID
- Expose the final device ID (preconfigured or auto-generated), model number, serial number, and appliance type to callers

### 1.3 Not Responsible For

- Configuring the MQTT adapter with the resulting device ID (caller's job)
- Managing bridge lifecycle or startup sequencing
- Reading any ERDs beyond the three identity ERDs

---

## 2. Initialization

### 2.1 Init

```cpp
void DeviceIdentityManager::init(
    const char* configured_id,
    i_tiny_gea3_erd_client_t* erd_client,
    uint8_t host_address);
```

| Parameter | Description |
|-----------|-------------|
| `configured_id` | Optional pre-configured device ID from YAML. If non-null and non-empty, it is stored but does not skip the ERD read sequence. |
| `erd_client` | GEA3 ERD client interface used to queue reads. |
| `host_address` | The appliance's GEA bus address for read requests. |

`init()` stores all parameters, resets state to `DEVICE_ID_STATE_READING_APPLIANCE_TYPE`, and immediately queues the first ERD read (0x0008). A pre-configured device ID is logged at info level but does not alter the read sequence.

### 2.2 Re-Init

Calling `init()` again resets state and re-queues the first read. The previous `erd_client` pointer is replaced; there is no explicit cleanup of the old client.

---

## 3. State Machine

The manager has four states and advances linearly through three ERD reads. On failure, the current read is retried in place — the state does not advance.

### 3.1 States

| State | Waiting For | On Success | On Failure |
|-------|-------------|------------|------------|
| `DEVICE_ID_STATE_READING_APPLIANCE_TYPE` | ERD 0x0008 | → `DEVICE_ID_STATE_READING_MODEL_NUMBER` | Retry 0x0008 |
| `DEVICE_ID_STATE_READING_MODEL_NUMBER` | ERD 0x0001 | → `DEVICE_ID_STATE_READING_SERIAL_NUMBER` | Retry 0x0001 |
| `DEVICE_ID_STATE_READING_SERIAL_NUMBER` | ERD 0x0002 | Assemble ID → `DEVICE_ID_STATE_COMPLETE` | Retry 0x0002 |
| `DEVICE_ID_STATE_COMPLETE` | — | — | — |

### 3.2 State Diagram

```
DEVICE_ID_STATE_READING_APPLIANCE_TYPE (0x0008)
  ├─ read completed → store appliance_type_, → DEVICE_ID_STATE_READING_MODEL_NUMBER
  └─ read failed    → retry 0x0008

DEVICE_ID_STATE_READING_MODEL_NUMBER (0x0001)
  ├─ read completed → store model_number_, → DEVICE_ID_STATE_READING_SERIAL_NUMBER
  └─ read failed    → retry 0x0001

DEVICE_ID_STATE_READING_SERIAL_NUMBER (0x0002)
  ├─ read completed → store serial_number_, assemble generated_device_id_, → DEVICE_ID_STATE_COMPLETE
  └─ read failed    → retry 0x0002

DEVICE_ID_STATE_COMPLETE
  └─ get_device_id() returns preconfigured or auto-generated ID
```

### 3.3 Self-Driving Behavior

The manager is event-driven, not polled:

- `on_erd_read_completed(erd, data, size)` — stores the value, transitions state, and queues the next ERD read via `try_queue_read_()`.
- `on_erd_read_failed(erd)` — stays in the current state and re-queues the same ERD read via `try_queue_read_()`. Retries indefinitely.

Both methods call `try_queue_read_()`, which delegates to `tiny_gea3_erd_client_read()`. If the ERD client queue is full, the read is silently dropped and the manager waits for the next event to retry.

---

## 4. Device ID Format

### 4.1 Auto-Generated ID

```
{appliance_type_string}_{sanitized_model}_{sanitized_serial}
```

- **Appliance type string:** produced by `appliance_type_to_string(appliance_type_)` — a generated function mapping the raw appliance type byte to a human-readable string (e.g., `"refrigerator"`, `"dishwasher"`).
- **Sanitized model:** `bytes_to_string_()` converts raw model number bytes to a string (stopping at null bytes), strips trailing `'_'` padding characters, then `sanitize_for_mqtt_topic_()` replaces MQTT-unsafe characters with `_`.
- **Sanitized serial:** `bytes_to_string_()` converts raw serial number bytes to a string (stopping at null bytes), strips trailing `'_'` padding characters, then `sanitize_for_mqtt_topic_()` replaces MQTT-unsafe characters with `_`.

### 4.2 Preconfigured ID

If `configured_device_id_` is non-empty, `get_device_id()` returns it verbatim — no sanitization is applied. The preconfigured ID completely replaces the auto-generated one.

### 4.3 ID Selection

```cpp
const char* get_device_id() const;
```

Returns `configured_device_id_` if `has_configured_device_id_` is true; otherwise returns `generated_device_id_`.

---

## 5. MQTT Sanitization

### 5.1 Characters Replaced

The `sanitize_for_mqtt_topic_()` function replaces the following with `_`:

| Character | Reason |
|-----------|--------|
| `+` | MQTT wildcard character |
| `#` | MQTT multi-level wildcard |
| `/` | MQTT topic separator |
| `$` | MQTT shared subscription prefix |
| ` ` (space) | Not safe in topics |
| `< 0x20` | Non-printable control characters |
| `> 0x7E` | Non-ASCII / extended characters |

All other printable ASCII characters (0x21–0x7E, excluding the special characters above) are preserved.

### 5.2 Empty Result

If sanitization produces an empty string (e.g., the input was entirely composed of replaced characters), the result is `"Unknown"`.

---

```cpp
enum DeviceIdState {
  DEVICE_ID_STATE_READING_APPLIANCE_TYPE,
  DEVICE_ID_STATE_READING_MODEL_NUMBER,
  DEVICE_ID_STATE_READING_SERIAL_NUMBER,
  DEVICE_ID_STATE_COMPLETE,
};
```

```cpp
class DeviceIdentityManager {
    DeviceIdState state_;                        // Current state in the read sequence
    bool has_configured_device_id_;              // True if a pre-configured ID was provided
    char configured_device_id_[92];              // Pre-configured ID from YAML (if any)
    char generated_device_id_[92];               // Auto-generated ID: type_model_serial
    uint8_t appliance_type_;                     // Raw appliance type byte (from ERD 0x0008)
    char model_number_[64];                      // Raw model number string (from ERD 0x0001)
    char serial_number_[64];                     // Raw serial number string (from ERD 0x0002)
    tiny_gea3_erd_client_request_id_t pending_request_id_;  // Last queued read request ID

    i_tiny_gea3_erd_client_t* erd_client_;      // ERD client interface
    uint8_t host_address_;                      // Appliance GEA bus address
};
```

---

## 7. Public API

| Method | Return | Description |
|--------|--------|-------------|
| `init(configured_id, erd_client, host_address)` | `void` | Initialize and start the read sequence. |
| `on_erd_read_completed(erd, data, size)` | `void` | Callback: ERD read succeeded. Stores value, advances state, queues next read. |
| `on_erd_read_failed(erd)` | `void` | Callback: ERD read failed. Re-queues the same ERD. Retries indefinitely. |
| `get_state()` | `DeviceIdState` | Returns current state. Used by the startup HSM to detect completion. |
| `cleanup()` | `void` | Resets all state to initial values. Safe to call multiple times. |
| `get_device_id()` | `const char*` | Returns preconfigured ID if set, otherwise auto-generated ID. |
| `get_model_number()` | `const char*` | Returns raw model number string. |
| `get_appliance_type()` | `uint8_t` | Returns raw appliance type byte (from ERD 0x0008). |
| `get_serial_number()` | `const char*` | Returns raw serial number string. |
---

## 8. Invariants

1. **Always reads ERDs:** The read sequence is always executed, regardless of whether a pre-configured device ID exists. The preconfigured ID is only consulted by `get_device_id()` after completion.
2. **Indefinite retry:** A failed ERD read is retried indefinitely. The manager never advances past a failed read.
3. **One read at a time:** Only one ERD read is outstanding at any time. The next read is queued only after the previous completes or fails.
4. **Linear progression:** The read order is fixed: appliance type → model number → serial number. The manager never skips or reorders reads.
5. **No timeout:** The manager has no internal timeout. If the appliance is unresponsive, the manager remains in the current reading state indefinitely. The startup HSM handles timeout and recovery.
6. **Sanitization only on model and serial:** The appliance type string (from `appliance_type_to_string()`) is not sanitized — it is a generated, known-safe string. Only model number and serial number are sanitized for MQTT topic safety.

---

## 9. Integration with GeappliancesBridge

### 9.1 Initialization

The bridge calls `init()` during startup, passing the configured device ID (if any), the active ERD client from the autodiscovery manager, and the host address.

### 9.2 Completion Detection

The startup HSM notices completion by polling: `startup_state_device_id`'s `signal_run_loop` handler checks `is_device_id_complete()`, which is true once the state is `DEVICE_ID_STATE_COMPLETE`. The bridge does not signal the HSM when a read completes.

### 9.3 Accessors

The bridge reads `get_model_number()`, `get_serial_number()`, `get_appliance_type()`, and `get_device_id()` after the state is `DEVICE_ID_STATE_COMPLETE`.

---

## 10. Dependencies

| Dependency | Purpose |
|------------|---------|
| `i_tiny_gea3_erd_client` / `tiny_gea3_erd_client.h` | ERD client interface for queuing reads |
| `appliance_type_to_string()` | Generated function mapping appliance type byte to string |
| `geappliances_bridge_constants.h` | ERD constants (`ERD_APPLIANCE_TYPE`, `ERD_MODEL_NUMBER`, `ERD_SERIAL_NUMBER`) |
| `esphome/core/log.h` | Logging (`ESP_LOGI`, `ESP_LOGW`) |

---

## 11. Known Limitations

1. **No timeout:** If the appliance is unresponsive, the manager retries indefinitely. The startup HSM provides the timeout boundary.
2. **No batch reads:** Each ERD is read individually, one at a time. The ERD client does not support batch identity reads.
3. **Queue full silently drops:** If `try_queue_read_()` fails because the ERD client queue is full, the read is dropped and the manager waits for the next event callback to retry. There is no backoff timer.
4. **Preconfigured ID not sanitized:** A pre-configured device ID is used verbatim — if it contains MQTT-unsafe characters, the caller is responsible for ensuring topic safety.
5. **No validation of ERD data size:** If an ERD returns fewer bytes than expected (e.g., appliance type is 0 bytes), the read is treated as a success with whatever data was returned. The `on_erd_read_completed()` handler guards against empty appliance type data (`size < 1` returns without advancing).
