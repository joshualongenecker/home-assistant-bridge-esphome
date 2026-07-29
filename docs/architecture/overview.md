# Architecture Overview

## System Summary

The GE Appliances Bridge is an ESPHome custom component that runs on an ESP32 microcontroller and bridges GE appliances (via GEA2/GEA3 serial bus protocols) to Home Assistant (via MQTT). The component owns the full lifecycle: discovering the appliance on the serial bus, reading its identity, determining which ERDs are available through feature bit parsing, and then continuously publishing those values to MQTT while accepting write commands from Home Assistant. Data flows in both directions — appliance state is published to `geappliances/{device_id}/erd/0x{ERD}/value` topics, and write commands are accepted on `geappliances/{device_id}/erd/0x{ERD}/write` topics.

## C4 Level 1 — System Context

```mermaid
%%{init: {"theme":"neutral","themeVariables":{"primaryColor":"#4a90d9","primaryBorderColor":"#2c6a9e","primaryTextColor":"#1a1a1a","secondaryColor":"#d9e8f5","tertiaryColor":"#f0f0f0","lineColor":"#999999","clusterBkg":"#f5f5f5","clusterBorder":"#aaaaaa","fontFamily":"monospace","nodeBorder":"#888888","edgeLabelBackground":"#ffffff","actorBkg":"#e8e8e8","actorBorder":"#999999","actorTextColor":"#1a1a1a"}}}%%
graph TB
    USER["User<br/>(Home Assistant UI)"]
    HA["Home Assistant"]
    BROKER["MQTT Broker"]
    BRIDGE["ESP32 Bridge<br/>(ESPHome Component)"]
    APPLIANCE["GE Appliance<br/>(GEA2/GEA3 Serial Bus)"]

    USER -->|"Interacts with entities"| HA
    HA -->|"Subscribes to value topics<br/>Publishes write commands"| BROKER
    BROKER -->|"Publishes ERD values<br/>Forwards write requests"| BRIDGE
    BRIDGE -->|"Reads/writes ERDs<br/>GEA2 (19200 baud) / GEA3 (230400 baud)"| APPLIANCE

    classDef actor fill:#e8e8e8,stroke:#999999
    classDef system fill:#d9e8f5,stroke:#2c6a9e
    classDef external fill:#e8f5e8,stroke:#388e3c

    class USER actor
    class BRIDGE system
    class HA,BROKER,APPLIANCE external
```

The system has four external actors and one system boundary:

- **User** interacts with Home Assistant's UI to monitor and control the appliance.
- **Home Assistant** creates entities via MQTT discovery and routes user actions to MQTT topics.
- **MQTT Broker** (e.g. Mosquitto) relays messages between the bridge and Home Assistant.
- **GE Appliance** exposes its state and controls over a serial bus using the GEA2 or GEA3 protocol.
- **ESP32 Bridge** (this system) sits between the appliance and the MQTT broker, translating between protocols.

## C4 Level 2 — Container

```mermaid
%%{init: {"theme":"neutral","themeVariables":{"primaryColor":"#4a90d9","primaryBorderColor":"#2c6a9e","primaryTextColor":"#1a1a1a","secondaryColor":"#d9e8f5","tertiaryColor":"#f0f0f0","lineColor":"#999999","clusterBkg":"#f5f5f5","clusterBorder":"#aaaaaa","fontFamily":"monospace","nodeBorder":"#888888","edgeLabelBackground":"#ffffff","actorBkg":"#e8e8e8","actorBorder":"#999999","actorTextColor":"#1a1a1a"}}}%%
graph TB
    subgraph ESP32["ESP32 Microcontroller"]
        ESPHOME["ESPHome Framework<br/>(YAML Config, UART, MQTT Client)"]
        BRIDGE_COMP["Bridge Component<br/>(geappliances_bridge)"]
        GEA_STACK["GEA Protocol Stack<br/>(lib/tiny, lib/tiny-gea-api submodules)"]
        DISCOVERY["Embedded Discovery Data<br/>(ha_discovery_data.h)"]

        ESPHOME -->|"UARTComponent, global_mqtt_client"| BRIDGE_COMP
        ESPHOME -->|"UARTComponent"| GEA_STACK
        BRIDGE_COMP -->|"i_tiny_uart_t, i_tiny_gea3_erd_client_t"| GEA_STACK
        BRIDGE_COMP -->|"Decompresses at startup"| DISCOVERY
    end

    BROKER["MQTT Broker"]

    BRIDGE_COMP -->|"Publishes to geappliances/{device_id}/erd/..."| BROKER
    BROKER -->|"Write requests on geappliances/{device_id}/erd/.../write"| BRIDGE_COMP

    classDef hardware fill:#fff3e0,stroke:#e65100
    classDef framework fill:#e8e8f0,stroke:#5c6bc0
    classDef component fill:#f3e5f5,stroke:#7b1fa2
    classDef submodule fill:#e8f5e9,stroke:#2e7d32
    classDef external fill:#fce4ec,stroke:#880e4f

    class ESP32 hardware
    class ESPHOME framework
    class BRIDGE_COMP component
    class GEA_STACK submodule
    class DISCOVERY component
    class BROKER external
```

### Container Descriptions

**ESPHome Framework** provides the runtime environment: YAML configuration parsing,
the `UARTComponent` for serial I/O, the `MQTTClient` for MQTT connectivity, and the
component lifecycle (`setup()`, `loop()`, `dump_config()`).

**Bridge Component** (`components/geappliances_bridge/`) is the custom ESPHome
component. It contains all application logic: startup sequencing (via a state
machine), autodiscovery, device identity, feature bit parsing, data bridges
(polling, subscription, write), the ERD cache, and MQTT publishing. Data-path
modules use C structs with vtables; ESPHome integration layers use C++ classes.

**GEA Protocol Stack** is provided by the `lib/tiny` and `lib/tiny-gea-api` git
submodules. It implements the UART HAL, GEA3 and GEA2 protocol interfaces, ERD
clients, state machines, timers, and event system. The bridge interacts with it
through well-defined C interfaces (`i_tiny_uart_t`, `i_tiny_gea3_erd_client_t`,
`i_tiny_gea2_erd_client_t`, `i_tiny_time_source_t`).

**Embedded Discovery Data** (`ha_discovery_data.h`) is a build-time generated file
containing compressed Home Assistant MQTT discovery entity definitions. Discovery
payloads are published on discovery data changes (hash or device ID) or when the Discovery Refresh button is
pressed — normal boots skip discovery since topics are retained by the MQTT broker.

## Key Design Decisions

### Single-core constraints

The bridge targets ESP32-C3 and other single-core ESP32 variants. Synchronization
uses a mixed strategy: `vPortEnterCritical`/`vPortExitCritical` for short
critical sections in the discovery cleanup path, and `xSemaphoreCreateMutex`
for the ERD cache MQTT publisher's state fields (`mqtt_connected`, `paused`,
`publish_index`). ERD cache entries themselves require no locking — the main
loop writes and the background publisher reads, with ordering guaranteed by
the round-robin index. The `mark_published()` and `tick_cooldowns()` functions
operate on disjoint entry sets (cleared vs. pending `update_required`), so they
also require no locking. The protocol stack tight loop always runs before MQTT
operations to avoid starving UART processing — a blocking MQTT call could
delay response processing past the appliance's timeout window.

### No dynamic collections

The codebase avoids `std::set`, `std::vector`, and other heap-allocating containers.
Fixed-capacity arrays replace them: the ERD cache holds 200 entries in a static
array; ERD sets use sorted arrays with binary search; the custom ERD list caps at
64 entries; the polling list uses a fixed buffer. This eliminates heap fragmentation
risk and makes memory usage predictable.

### Static arena data storage

All ERD data is stored in a 4 KB static arena (bump allocator) within the
cache struct, regardless of value size. Each cache entry holds an offset and
size into the arena. At registration, a new entry claims a contiguous slice
via a bump pointer. Subsequent updates are in-place `memcpy` — no alloc or
free. This keeps all ERD data entirely off the heap with a single allocation
strategy.

### Embedded discovery data

Home Assistant MQTT discovery payloads are generated by a build-time pipeline
(`scripts/ha_discovery/run_pipeline.py`), compressed with zlib, and embedded as
`const uint8_t[]` arrays in `ha_discovery_data.h`. At runtime, the bridge
decompresses individual chunks into pre-allocated buffers (peak memory ~45 KB:
8 KB payload + 18 KB decompress + 18 KB line buffer + 1.3 KB sorted ERD array).
No network access or file I/O is needed at runtime. Discovery runs only on discovery data changes (hash or device ID) or when the Discovery Refresh button is pressed — normal boots skip it, as topics are retained by the MQTT broker.

### Task Watchdog avoidance

Long-running operations are split across multiple timer ticks to avoid triggering
the ESP32 Task Watchdog Timer. Feature bit parsing processes ~4 bitmasks per call
across ~5 timer callbacks for the common feature ERD (0x0092), plus ~10 more
callbacks for appliance-specific ERDs (0x0093–0x0097, 0x0109–0x010D). The polling
bridge budgets MQTT operations per loop tick. The main loop feeds the watchdog
after the protocol stack tight loop and after the HSM run_loop signal via
`esp_task_wdt_reset()`.

### C/C++ split by layer

The codebase follows a strict convention: data-path components (ERD cache,
bridges, interfaces, publishers, discovery) use C structs with vtable-style
function pointers for polymorphism (`i_tiny_gea3_erd_client_t`, `i_mqtt_client_t`,
`i_tiny_uart_t`), keeping them portable and testable without C++ overhead. The
ESPHome integration layer uses C++ adapter classes that implement these C
interfaces, bridging ESPHome APIs to the data path. The `IBridgeServices`
abstract class decouples the startup HSM from the concrete `GeappliancesBridge`.

### Event-driven architecture

Components are wired together via a lightweight pub-sub event system
(`tiny_event_subscribe`) rather than direct function pointers. The ERD client
publishes activity events (`read_completed`, `read_failed`,
`subscription_publication_received`); the bridge subscribes with lambda callbacks
that route data to the appropriate manager. The GEA2-to-GEA3 adapter normalizes
GEA2 events into the same GEA3 activity interface, so the bridge's event
handlers are protocol-agnostic.

### Bridge mode selection

The bridge supports three operating modes — POLL, SUBSCRIBE, and AUTO — selected
at initialization time. GEA2 appliances are forced into polling mode since they
do not support subscriptions. AUTO mode attempts subscription first and falls
back to polling via two independent triggers: (a) no subscription publications
within a 2-second quiet period, or (b) three consecutive subscription request
failures (`subscribe_failure_count >= 3`).
The fallback tears down the subscription bridge and re-initializes a polling
bridge as a replacement.

### Shared ERD cache

The ERD cache serves as the decoupling boundary between the appliance-facing
and MQTT-facing halves of the system. The appliance side (subscription bridge,
polling bridge, write bridge) interacts exclusively with the cache: it writes
new values on reads and reads cached values on writes. The MQTT side (publisher,
discovery) interacts exclusively with the cache: it drains `update_required`
entries for publishing and reads cached values for discovery payloads. Neither
side knows about the other — the appliance side has no MQTT dependency and the
MQTT side has no appliance protocol dependency. In AUTO mode, subscription
publications and custom-ERD polling results coexist in the same cache; later
writes overwrite earlier values by ERD ID. The publisher drains the cache via
a round-robin index, ensuring fair distribution across all ERDs.

### Round-robin publishing with background task

Publishing runs in a dedicated FreeRTOS background task created with
`xTaskCreateStaticPinnedToCore` on dual-core ESP32 (pinned to Core 1,
matching ESPHome's main loop) or `xTaskCreateStatic` on single-core
(zero heap allocation in both cases). The task blocks on a binary semaphore
signaled by the main loop, acquires a state mutex to safely read shared fields,
publishes one entry per wake, then releases the mutex. Pre-allocated buffers on
the struct avoid stack overflow. On MQTT disconnect the publisher pauses; on
reconnect after more than 60 seconds it forces a full republish of all cached
ERDs to handle broker restarts that lose their retained store.

### GEA2 tick-counter time source

The GEA2 interface requires millisecond-accurate timers, but ESPHome's `loop()`
calls arrive at ~50 ms intervals. A tick-counter time source increments a
counter once per real millisecond inside the GEA2 tight loop (100 ms wall-clock,
200 ms hard cap), catching up missed milliseconds by publishing a
`msec_interrupt` event for each. This keeps GEA2 timers advancing by at most
1 ms per event regardless of the framework's loop cadence.

### Write bridge gating

The write bridge gates all ERD write requests on whether the appliance host
address has been resolved from discovery. If the host address is still the
broadcast address (`0xFF`), writes are rejected with a "not supported" error.
The write bridge uses a two-state HSM (ready/writing) with request-id tracking
to handle out-of-order completions and stale responses.

## Detailed Documentation

| Document | Description |
|---|---|
| [Startup Sequence](./startup-sequence.md) | 8-phase startup HSM sequence diagram |
| [Data Flow](./data-flow.md) | Read path, write path, discovery flow |
| [Module Descriptions](../module_descriptions/) | Lightweight summary for each module |
| [Specifications](../spec/) | Detailed behavioral contracts per module |
