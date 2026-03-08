# GE Appliances Bridge — Technical Reference

> **Audience**: Systems engineers familiar with embedded C/C++, UART serial buses, and MQTT.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Hardware Interfaces](#2-hardware-interfaces)
3. [Software Architecture](#3-software-architecture)
4. [Configuration Parameters](#4-configuration-parameters)
5. [Startup Sequence](#5-startup-sequence)
6. [Autodiscovery State Machine](#6-autodiscovery-state-machine)
7. [Device ID Generation](#7-device-id-generation)
8. [MQTT Bridge Modes](#8-mqtt-bridge-modes)
9. [Per-Board Bridge Initialization](#9-per-board-bridge-initialization)
10. [Event Handlers](#10-event-handlers)
11. [Auto-Mode Subscription Watchdog](#11-auto-mode-subscription-watchdog)
12. [Data Flow Diagram](#12-data-flow-diagram)
13. [Key Constants and Limits](#13-key-constants-and-limits)
14. [Known Exclusions](#14-known-exclusions)
15. [ESPHome Integration Points](#15-esphome-integration-points)

---

## 1. Overview

`GeappliancesBridge` is an ESPHome `Component` that bridges one or two GE Appliance serial buses (GEA3 and/or GEA2) to MQTT. It:

- **Discovers** appliance boards on the bus automatically after startup.
- **Generates a unique device ID** for the primary bridged board from its appliance type, model number, and serial number ERDs.
- **Publishes ERD (Entity Reference Designator) values** for that primary board to MQTT topics and accepts write commands from MQTT.
- **Currently bridges a single board per bus** via one MQTT bridge instance, even though internal limits such as `MAX_BOARDS = 8` exist.

The component operates entirely within ESPHome's cooperative `loop()` — there are no RTOS tasks or interrupts. All timing is driven by `tiny_timer_group`.

---

## 2. Hardware Interfaces

### GEA3 Bus (primary)
| Parameter | Value |
|-----------|-------|
| Baud rate | 230,400 bps |
| Client address | `0xE4` (the ESP32's own address on the bus) |
| Protocol | GEA3 (RS-485-like, framed by `tiny_gea3_interface`) |
| ERD client timeouts | 250 ms per request, 10 retries |

### GEA2 Bus (optional)
| Parameter | Value |
|-----------|-------|
| Client address | `0xE4` (same as GEA3) |
| Protocol | GEA2 (older GE bus, framed by `tiny_gea2_interface`) |
| ERD client timeouts | 250 ms per request, 0 retries (no automatic retries at ERD client layer) |
| Timer | 1 ms tick events are manually published from within the 200 ms tight loop (no dedicated periodic `tiny_timer`) |

GEA2 is only initialized when `gea2_uart_id` is configured in YAML. If GEA2 is absent, all GEA2 state machines are skipped.

---

## 3. Software Architecture

```
GeappliancesBridge (ESPHome Component)
│
├── setup()
│   ├── Initialize tiny_timer_group
│   ├── Initialize GEA3 UART adapter → GEA3 interface → GEA3 ERD client
│   ├── Subscribe: GEA3 ERD client activity  →  handle_erd_client_activity_()
│   ├── Subscribe: GEA3 raw packet receive   →  handle_gea3_raw_packet_()
│   └── (if gea2_uart configured)
│       ├── Initialize GEA2 UART adapter → GEA2 interface → GEA2 ERD client
│       ├── Subscribe: GEA2 ERD client activity  →  handle_gea2_erd_client_activity_()
│       └── Subscribe: GEA2 raw packet receive   →  handle_gea2_raw_packet_()
│
└── loop()  (called every ESPHome tick, non-blocking)
    ├── tiny_timer_group_run()        ← advances all tiny timers
    ├── tiny_gea3_interface_run()     ← processes UART RX/TX bytes
    ├── tiny_gea2_interface_run()     ← (if GEA2)
    ├── run_autodiscovery_()          ← discovery state machine
    ├── initialize_mqtt_bridge_()     ← one-shot, when device ID + MQTT ready
    ├── check_subscription_activity_() ← auto-mode watchdog
    └── device ID ERD read retries    ← queues next ERD read when state != IDLE
```

The three library layers under the bridge:

| Layer | Library type | Role |
|-------|-------------|------|
| `esphome_uart_adapter` | ESPHome shim | Feeds UART bytes to/from `tiny_gea*_interface` |
| `tiny_gea3_interface` / `tiny_gea2_interface` | C library | Framing, addressing, CRC |
| `tiny_gea3_erd_client` / `tiny_gea2_erd_client` | C library | Read/write/subscribe ERD requests, queuing, retries |

---

## 4. Configuration Parameters

| YAML key | Setter | Default | Description |
|----------|--------|---------|-------------|
| `gea3_uart_id` | `set_gea3_uart()` | `nullptr` | Optional GEA3 UART (at least one of `gea2_uart_id` / `gea3_uart_id` is required) |
| `gea2_uart_id` | `set_gea2_uart()` | `nullptr` | Optional GEA2 UART (at least one of `gea2_uart_id` / `gea3_uart_id` is required) |
| `device_id` | `set_device_id()` | `""` | Skip auto-generation; use literal string |
| `mode` | `set_mode()` | `AUTO` | `POLL=0`, `SUBSCRIBE=1`, `AUTO=2` |
| `polling_interval` | `set_polling_interval()` | `10000` ms | ERD poll period |
| `polling_onlypublish_onchange` | `set_polling_only_publish_on_change()` | `false` | When `true`, only publish ERD values to MQTT when the value changes (suppress duplicate publishes) |

The discovered appliance's address becomes `host_address_` and is used for all subsequent ERD reads/writes.

---

## 5. Startup Sequence

```
Power-on / ESPHome boot
       │
       ▼
setup() ──► Hardware init (UART adapters, GEA interfaces, ERD clients, subscriptions)
       │
       ▼
loop() begins polling
       │
       ├── (if device_id configured) ─► DEVICE_ID_STATE_COMPLETE
       │                                bridge_init_state_ = WAITING_FOR_MQTT
       │
       └── (no device_id)
              │
              ▼
        AUTODISCOVERY_WAITING_FOR_MQTT
              │
              ▼ (MQTT connects)
          AUTODISCOVERY_WAITING_5S  ← 5-second stabilization delay
                  │
                  ▼ (5 s elapsed)
        Autodiscovery begins
              │
              ▼
        Device ID generation
              │
              ▼
        initialize_mqtt_bridge_()
```

The 5-second delay (`STARTUP_DELAY_MS = 5000`) allows the appliance buses to fully initialize and the MQTT broker to stabilize before sending any bus traffic.

---

## 6. Autodiscovery State Machine

Discovery is a two-phase protocol. The full state machine is:

```
AUTODISCOVERY_WAITING_FOR_MQTT
        │  (MQTT connect event)
        ▼
AUTODISCOVERY_WAITING_5S
        │  (5 s elapsed)
        ▼
┌──────────────────────────────────────────────────────────────────────┐
│  GEA3 PATH                                                           │
│                                                                      │
│  GEA3_PING_PENDING ──────► GEA3_PING_WAITING                        │
│  (reset counters,          (send Cmd=0x01 broadcast 5× at 2s         │
│   send first ping)          intervals; collect all responders        │
│                             via raw packet subscription;             │
│                             5 s window)                              │
│           │ no boards ◄────────────────────────────┘                │
│           │ (retry GEA3 or try GEA2)                                │
│           │ boards found                                             │
│           ▼                                                          │
│  GEA3_ERD_CHECK_PENDING ──► GEA3_ERD_CHECK_WAITING                  │
│  (for each board in         (unicast ERD 0x0008 read;                │
│   response list,            3 s timeout; advance on                  │
│   queue unicast read)       read_completed / read_failed only)       │
│           │ all boards checked                                       │
│           │ ≥1 appliance found → AUTODISCOVERY_COMPLETE             │
│           │ 0 appliances found → retry GEA3 or try GEA2            │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│  GEA2 PATH (mirror of GEA3 path; triggered when GEA3 finds nothing) │
│                                                                      │
│  GEA2_PING_PENDING ──────► GEA2_PING_WAITING                        │
│  (send ERD 0x0008 broadcast  (collect all responders;                │
│   — GEA2 has no Cmd=0x01)    5 s window)                            │
│           │                                                          │
│           ▼                                                          │
│  GEA2_ERD_CHECK_PENDING ──► GEA2_ERD_CHECK_WAITING                  │
│           │ all boards checked                                       │
│           │ ≥1 appliance found → AUTODISCOVERY_COMPLETE             │
│           │ 0 appliances found → retry GEA2 or restart GEA3        │
└──────────────────────────────────────────────────────────────────────┘

AUTODISCOVERY_COMPLETE → start_device_id_generation_()
```

### Phase 1 — Ping

| Parameter | GEA3 | GEA2 |
|-----------|------|------|
| Message sent | Raw `Src=0xE4, Dst=0xFF, Cmd=0x01` (no data) | ERD `0x0008` read to `Dst=0xFF` via ERD client |
| Repetitions | 5× at 2-second intervals | 5× at 2-second intervals |
| Window | 5 seconds | 5 seconds |
| Collection method | Raw packet subscription (`handle_gea3_raw_packet_`) | Raw packet subscription (`handle_gea2_raw_packet_`) |
| Result | `gea3_board_response_list_[]` + `gea3_board_response_count_` | `gea2_board_response_list_[]` + `gea2_board_response_count_` |

Any board that transmits *any* packet back to `0xE4` during the window is added to the response list (deduplicated). Addresses `0xBE`, `0xBF`, `0xE4` (self), and `0xFF` (broadcast) are excluded.

### Phase 2 — Appliance Verification

For each address in the response list, the bridge:

1. Sends a **unicast** ERD `0x0008` (appliance type) read to that specific address.
2. Waits in `ERD_CHECK_WAITING` for the ERD client activity event.
3. **Only** a `read_completed` or `read_failed` event for `ERD_APPLIANCE_TYPE` (0x0008) advances the state machine. All other event types (subscription publications, host-came-online, write completions, etc.) are silently ignored — this prevents a race condition where an out-of-band subscription event prematurely skips the board.
4. A 3-second per-board safety timeout (`ERD_CHECK_TIMEOUT_MS`) moves the state machine forward if the ERD client does not fire.

Boards that respond are added to `gea3_discovered_addresses_[]` / `gea3_discovered_count_` (or GEA2 equivalents). Boards that fail or time out are skipped.

---

## 7. Device ID Generation

After autodiscovery (`AUTODISCOVERY_COMPLETE`), a sequential per-board device ID generation loop runs.

### State machine

```
start_device_id_generation_()
        │
        ▼
DEVICE_ID_STATE_READING_APPLIANCE_TYPE  (ERD 0x0008)
        │ read_completed
        ▼
DEVICE_ID_STATE_READING_MODEL_NUMBER    (ERD 0x0001, 32-byte string)
        │ read_completed
        ▼
DEVICE_ID_STATE_READING_SERIAL_NUMBER   (ERD 0x0002, 32-byte string)
        │ read_completed
        ▼
  device_id = appliance_type_name + "_" + sanitized_model + "_" + sanitized_serial
  store board_device_ids_[i] and board_appliance_types_[i]
        │
        ├── more boards? → advance device_id_gen_address_ → repeat loop
        │
        └── all done → DEVICE_ID_STATE_COMPLETE
                      → bridge_init_state_ = WAITING_FOR_MQTT
```

### ERD reads

Each ERD read is issued from `loop()` by calling `try_read_erd_with_retry_()`. If the ERD client queue is full, the call returns `false` and is retried on the next `loop()` iteration (up to `MAX_READ_RETRIES = 1000`). The state variable is set to `DEVICE_ID_STATE_IDLE` while the response is in flight so `loop()` does not re-queue a duplicate request.

### Device ID format

```
<ApplianceTypeName>_<ModelNumber>_<SerialNumber>
```

- `ApplianceTypeName` is produced by the auto-generated `appliance_type_to_string()` function (defined outside this file).
- Model and serial strings are sanitized: characters `+`, `#`, `/`, `$`, ` ` (space), non-printable bytes, and extended ASCII are replaced with `_`.
- The **primary board** (matching `gea3_address_preference_`) always gets `final_device_id_` without a suffix.
- If `device_id` was pre-configured in YAML, generation is skipped entirely and the configured string is used directly.

---

## 8. MQTT Bridge Modes

| Mode | `BridgeMode` value | Behaviour |
|------|-------------------|-----------|
| **Polling** | `BRIDGE_MODE_POLL = 0` | `mqtt_bridge_polling_init()` called; ERDs polled every `polling_interval_ms` |
| **Subscription** | `BRIDGE_MODE_SUBSCRIBE = 1` | `mqtt_bridge_init()` called; appliance pushes ERD updates via GEA3 subscription publications |
| **Auto** | `BRIDGE_MODE_AUTO = 2` | Starts in subscription mode; watchdog falls back to polling after 30 s if no subscription publications arrive |

### Auto-mode fallback

In `BRIDGE_MODE_AUTO`, `check_subscription_activity_()` is called every `loop()` while `subscription_mode_active_ == true`. If no `subscription_publication_received` event from `host_address_` has been observed within 30 seconds:

1. All subscription bridges (`mqtt_bridge_t`) are destroyed via `mqtt_bridge_destroy()`.
2. Polling bridges (`mqtt_bridge_polling_t`) are initialized in their place for every board.
3. `subscription_mode_active_` is set to `false` so the watchdog no longer runs.

---

## 9. Per-Board Bridge Initialization

`initialize_mqtt_bridge_()` is called once when both conditions are true:

- `bridge_init_state_ == BRIDGE_INIT_STATE_WAITING_FOR_MQTT`
- MQTT client is connected

It iterates `discovered_count` boards (from `gea3_discovered_addresses_[]` or `gea2_discovered_addresses_[]`) and for each board:

1. **Allocates** one `esphome_mqtt_client_adapter_t`, one `mqtt_bridge_t` (or `mqtt_bridge_polling_t`) from the pre-allocated arrays in the class.
2. **Calls** `esphome_mqtt_client_adapter_init()` with the board's unique device ID string — this is the MQTT topic root.
3. **Calls** `mqtt_bridge_init()` (subscription) or `mqtt_bridge_polling_init()` (polling) with:
   - The shared `tiny_timer_group_t`
   - The shared `tiny_gea3_erd_client_t`
   - The per-board `mqtt_client_adapter`
   - `board_address` — the board's GEA3/GEA2 address
   - `board_appliance_types_[i]` — passed to polling mode to select the correct ERD polling list

`bridge_count_` tracks the number of initialized bridges so `on_mqtt_connected_()` and `notify_mqtt_disconnected_()` can iterate all of them.

---

## 10. Event Handlers

### `handle_gea3_raw_packet_(packet)`

Called for every packet received on the GEA3 bus.

- **During `AUTODISCOVERY_GEA3_PING_WAITING`**: logs the raw packet (DEBUG level), then adds `packet->source` to `gea3_board_response_list_[]` if not already present. Excluded: self (`0xE4`), broadcast (`0xFF`), `0xBE`, `0xBF`.
- **All other states**: returns immediately (no-op).

### `handle_gea2_raw_packet_(packet)`

Mirror of the GEA3 handler but operates during `AUTODISCOVERY_GEA2_PING_WAITING` and writes to `gea2_board_response_list_[]`.

### `handle_erd_client_activity_(args)`

Called for every GEA3 ERD client event (read, write, subscribe completions; subscription publications; host-came-online).

Three independent responsibilities:

| State / condition | Action |
|-------------------|--------|
| `BRIDGE_MODE_AUTO` + `subscription_mode_active_` + event is `subscription_publication_received` from `host_address_` | Sets `subscription_activity_detected_ = true` (suppresses auto-mode fallback to polling) |
| `AUTODISCOVERY_GEA3_PING_WAITING` | Returns immediately (raw packet handler handles discovery) |
| `AUTODISCOVERY_GEA3_ERD_CHECK_WAITING` | If `read_completed` for `ERD_APPLIANCE_TYPE`: adds board to confirmed appliance list; if `read_failed` for `ERD_APPLIANCE_TYPE`: logs skip. Advances `gea3_board_check_index_` and returns to `ERD_CHECK_PENDING`. All other event types: `return` (no advance). |
| `!mqtt_bridge_initialized_` + address matches `device_id_gen_address_` | Drives device ID generation state machine (reads appliance type → model → serial) |

### `handle_gea2_erd_client_activity_(args)`

Mirror of the GEA3 activity handler for GEA2 events. Additionally handles GEA2-path device ID reads when `use_gea2_for_device_id_ == true`.

---

## 11. Auto-Mode Subscription Watchdog

`check_subscription_activity_()` runs in `loop()` only when `mode_ == BRIDGE_MODE_AUTO && subscription_mode_active_`.

```
elapsed = millis() - subscription_start_time_

if subscription_activity_detected_:
    return  (subscription is healthy, do nothing)

if elapsed >= 30000 ms:
    for each bridge i in [0 .. bridge_count_):
        mqtt_bridge_destroy(&mqtt_bridges_[i])
        mqtt_bridge_polling_init(&mqtt_bridge_pollings_[i], ..., board_address, appliance_type)
    subscription_mode_active_ = false
    log WARNING: "switched to polling mode"
```

The 30-second timeout uses unsigned 32-bit subtraction, so it wraps correctly when `millis()` overflows approximately every 49.7 days.

---

## 12. Data Flow Diagram

```
Appliance Bus (UART)
        │
        ▼
esphome_uart_adapter  ◄──────────────────────────────────────────►  tiny_gea3_interface
        │                                                                    │
        │                                                           tiny_gea3_erd_client
        │                                                                    │
        │  raw packet subscription                     ERD client activity subscription
        ▼                                                                    ▼
handle_gea3_raw_packet_()                              handle_erd_client_activity_()
        │                                                                    │
        │  (Phase 1: collect board addresses)           (Phase 2: confirm appliances)
        │                                               (Device ID: read ERDs per board)
        ▼                                                                    ▼
gea3_board_response_list_[]                            gea3_discovered_addresses_[]
                                                       board_device_ids_[]
                                                                            │
                                                                            ▼
                                                               initialize_mqtt_bridge_()
                                                                            │
                                               ┌────────────────────────────┴──────────────────────────────┐
                                               │  Board 0                                    Board N        │
                                               │  mqtt_client_adapters_[0]   …   mqtt_client_adapters_[N]  │
                                               │  mqtt_bridges_[0] or              mqtt_bridges_[N] or     │
                                               │  mqtt_bridge_pollings_[0]         mqtt_bridge_pollings_[N] │
                                               └───────────────────────────────────────────────────────────┘
                                                                            │
                                                                            ▼
                                                                     MQTT Broker
                                                                  (Home Assistant, etc.)
```

---

## 13. Key Constants and Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_BOARDS` | `8` | Maximum boards tracked per discovery cycle |
| `STARTUP_DELAY_MS` | `5,000 ms` | Delay after MQTT connect before discovery starts |
| `AUTODISCOVERY_BROADCAST_WINDOW_MS` | `5,000 ms` | Duration of Phase 1 ping window |
| `AUTODISCOVERY_POLL_COUNT` | `5` | Number of pings sent during Phase 1 |
| `AUTODISCOVERY_REPEAT_INTERVAL_MS` | `2,000 ms` | Interval between pings |
| `ERD_CHECK_TIMEOUT_MS` | `3,000 ms` | Per-board Phase 2 safety timeout |
| `SUBSCRIPTION_TIMEOUT_MS` | `30,000 ms` | Auto-mode: time before falling back to polling |
| `MAX_READ_RETRIES` | `1,000` | ERD read retries before giving up |
| `LOG_EVERY_N_RETRIES` | `50` | Log retry warnings every N attempts |
| GEA3 ERD client request timeout | `250 ms` | Per-request timeout |
| GEA3 ERD client retries | `10` | Retries per request |
| GEA2 ERD client request timeout | `250 ms` | Per-request timeout |
| GEA2 ERD client retries | `3` | Retries per request |

---

## 14. Known Exclusions

### Addresses always excluded from discovery

| Address | Reason |
|---------|--------|
| `0xE4` | The bridge's own address (self) |
| `0xFF` | GEA broadcast address |
| `0xBE` | Known non-appliance device (e.g., gateway/controller) |
| `0xBF` | Known non-appliance device |

### GEA3-only multi-board MQTT bridging

The per-board `mqtt_bridge_t` and `mqtt_bridge_polling_t` are driven by the **GEA3** ERD client only. GEA2 can discover boards and generate device IDs, but the resulting MQTT bridges still use the GEA3 ERD client for polling/subscription requests in the initialized bridges. Full GEA2 multi-board bridging is not yet implemented.

### No unsubscribe mechanism

The GEA3 protocol has no unsubscribe command. Subscriptions expire on the appliance side only when the client stops sending `retain_subscription` keepalives (which takes several seconds). This means subscription publications may arrive during the autodiscovery window, but the Phase 2 state handler explicitly ignores them (only `read_completed` / `read_failed` for `ERD_APPLIANCE_TYPE` advance the state machine), so they cause no harm.

---

## 15. ESPHome Integration Points

| ESPHome hook | What the bridge does |
|-------------|----------------------|
| `setup()` | Initializes all hardware and event subscriptions |
| `loop()` | Advances all state machines and timer groups — must run every tick |
| `dump_config()` | Logs current device ID, addresses, mode, polling interval to the ESPHome log |
| `get_setup_priority()` | Returns `setup_priority::DATA` (600) — runs after UART (600) and MQTT (50) setup |

MQTT connectivity is detected by polling `mqtt::global_mqtt_client->is_connected()` each loop. A rising edge (disconnected → connected) triggers `on_mqtt_connected_()`. There is no explicit disconnect handler in the loop; instead, `notify_mqtt_disconnected_()` is called proactively from `on_mqtt_connected_()` to force all MQTT adapters to re-register their ERD subscriptions after a reconnect.
