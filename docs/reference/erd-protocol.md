# ERD Protocol Reference

Overview of the GE Appliance (GEA) serial protocols and ERD data model.

## ERD (Entity-Relationship Data)

An ERD is a named data point on a GE appliance, identified by a 16-bit hex ID. Each ERD has:

| Property | Description |
|---|---|
| **ERD ID** | 16-bit identifier (e.g., `0x0001`, `0x7001`) |
| **Size** | 1–255 bytes (varies by ERD) |
| **Operations** | `read`, `write`, `publish` (varies by ERD) |
| **Data type** | Raw bytes; interpretation depends on the ERD definition |

### ERD Address Ranges

| Range | Category |
|---|---|
| `0x0000`-`0x00FF` | Common (all appliances) |
| `0x1000`-`0x1FFF` | Refrigeration |
| `0x2000`-`0x2FFF` | Laundry |
| `0x3000`-`0x3FFF` | Dishwasher |
| `0x4000`-`0x4FFF` | Water Heater |
| `0x5000`-`0x5FFF` | Range |
| `0x6000`-`0x6FFF` | Energy |
| `0x7000`-`0x7FFF` | Air Conditioning |
| `0x8000`-`0x8FFF` | Water Filter |
| `0x9000`-`0x9FFF` | Small Appliance |

### Well-Known ERDs

| ERD ID | Name | Size | Operations |
|---|---|---|---|
| `0x0001` | Model Number | 16 bytes | read |
| `0x0002` | Serial Number | 15 bytes | read |
| `0x0008` | Appliance Type | 1 byte | read |
| `0x0092`–`0x010D` | Feature Bit ERDs | varies | read |

## GEA3 Protocol

GEA3 is the primary serial protocol for modern GE appliances.

| Property | Value |
|---|---|
| **Baud rate** | 230400 |
| **Data format** | 8N1 (8 data bits, no parity, 1 stop bit) |
| **Voltage** | 3.3V TTL |
| **Connection** | RJ45 jack |
| **Addressing** | Each board has a unique address (0x00–0xFF); 0xFF is broadcast |
| **Subscription** | Supported — appliance pushes ERD changes |

### GEA3 Message Flow

1. **Read request:** Bridge sends a read command to the appliance's address with the ERD ID.
2. **Response:** Appliance sends the ERD data back with a status code.
3. **Write request:** Bridge sends a write command with ERD ID and data.
4. **Write response:** Appliance acknowledges success or failure.
5. **Subscribe:** Bridge sends a subscription request; appliance publishes ERD changes.

## GEA2 Protocol

GEA2 is the legacy protocol for older appliances.

| Property | Value |
|---|---|
| **Baud rate** | 19200 |
| **Data format** | 8N1 |
| **Voltage** | 3.3V TTL |
| **Connection** | Custom serial connector |
| **Subscription** | Not supported — polling only |

### GEA2 UART Configuration

GEA2 requires special UART settings for reliable communication:

```yaml
uart:
  - id: gea2_uart
    tx_pin: GPIO9
    rx_pin: GPIO10
    baud_rate: 19200
    rx_full_threshold: 1   # deliver each byte immediately
    rx_timeout: 1          # minimise idle-flush latency
```

**Why these settings matter:** GEA2's inter-byte timeout is 6 ms. The default ESP32 UART `rx_full_threshold` batches bytes (~10 ms gap), which exceeds the 6 ms timeout and causes the receive FSM to abort mid-packet. Setting `rx_full_threshold: 1` delivers each byte within ~0.1 ms, keeping the gap well below the limit.

## Protocol Abstraction

The bridge component interacts with both protocols through a unified interface (`i_tiny_gea3_erd_client_t`). When GEA2 is the active protocol, the `gea2_erd_client_adapter` wraps the GEA2 client and presents it as a GEA3 client. `subscribe()` and `retain_subscription()` are no-ops for GEA2.

## Wire Framing

Both protocols share one byte-level framing. Constants live in `lib/tiny-gea-api/include/tiny_gea_constants.h` and `tiny_gea_packet.h`:

| Element | Value |
|---|---|
| **STX** | `0xE2` (`tiny_gea_stx`) — resets the assembler and seeds the CRC |
| **ETX** | `0xE3` (`tiny_gea_etx`) — an ETX with no preceding STX is not a packet |
| **ESC** | `0xE0` (`tiny_gea_esc`) |
| **CRC seed** | `0x1021` (`tiny_gea_crc_seed`); a valid packet's running CRC reduces to **0** at ETX |
| **Packet layout** | destination, payload_length, source, then payload (`tiny_gea_packet_t`); the command byte is `payload[0]` |
| **Minimum packet** | 6 bytes — dst(1) + len(1) + src(1) + payload(1) + CRC(2) |

Escaping is exact, not "all high bytes": `needs_escape(byte)` is `(byte & 0xFC) == tiny_gea_esc`, so only `0xE0`, `0xE1`, `0xE2` and `0xE3` are escaped. A sender emits ESC first and the raw byte on the next send step; a receiver takes the byte following an ESC as-is.

The CRC is transmitted such that the accumulator lands on zero — that is the property the test doubles reproduce when they hand-build packets (`test/tests/autodiscovery_manager_test.cpp`).

| Command | GEA3 | GEA2 |
|---|---|---|
| **Read request** | `0xA0` | `0xF0` |
| **Read response** | `0xA1` | `0xF0` |

For GEA2 the request and response command bytes are identical, so direction is the only discriminator; a receiver must filter on payload shape instead. ERD identifiers inside a read response are two bytes, big-endian (`(buffer[6] << 8) | buffer[7]`). The receive-side state machine is specified in `docs/spec/gea2_erd_client_adapter.md` (Notes) and `docs/spec/autodiscovery_manager.md`.

## Byte Order

**GEA transmits ERD values MSB-first (big-endian).** `read_be64()` (`components/geappliances_bridge/geappliances_bridge_constants.h`) folds up to 8 bytes MSB-first into a `uint64_t`; a payload longer than 8 bytes is truncated at 8, not wrapped. Multi-byte ERD fields must be decoded from the left.

## MQTT Payload Encoding

ERD payloads cross MQTT as hexadecimal, two characters per byte:

| Direction | Encoding |
|---|---|
| **Appliance → Home Assistant** | one `snprintf(…, "%02x", byte)` per byte, published with length `2 * data_size` and the retained flag set (`erd_cache_mqtt_publisher.cpp`) |
| **Home Assistant → appliance** | byte pairs decoded with `sscanf(&payload[i], "%2x", …)` into a stack buffer of `ERD_CACHE_MAX_DATA_SIZE` bytes; decoding stops at the first non-hex pair or when the buffer is full (`esphome_mqtt_client_adapter.cpp`) |

The write path decodes into a local stack buffer before publishing the event, because a second MQTT message can overwrite the adapter's payload buffer before the first event is delivered (`docs/adr/0015-write-path-is-single-flight.md`).

The Home Assistant-side decode templates emitted by `scripts/ha_discovery/generators/generate_ha_discovery.py` operate on that hex string:

| Field type | Template behavior |
|---|---|
| Numeric (`u8`/`u16`/`u32`/`i8`/`i16`/`i32`) | `value[a:b] \| from_hex`; signed types get two's-complement sign extension — subtract `2 ** (bytes * 8)` once the value reaches half the range |
| `float32` | `value[a:b] \| from_hex \| unpack('>f') \| round(3)` — GEA multi-byte values are MSB-first IEEE-754 single precision. `float32` is the only type routed through `unpack`, and the current `appliance_api_erd_definitions_processed.json` contains no `float32` field, so this path stays dormant until a definition introduces one |
| `bool` | `'01' if value[a:b] != '00' else '00'` |
| `enum` | map keyed by hex of width `bytes * 2`, falling back to `Unknown` |

Numeric field widths present in the current definitions: 1, 2, 3, 4, 6, 7, 8, 10, 16, 22 and 32 bytes.

## Payload Size Ceiling

The 248-byte limit is one chain, not four independent constants:

| Layer | Limit | Defined by |
|---|---|---|
| On-wire packet | 255 bytes total, 7 bytes of overhead (STX, ETX, CRC MSB + LSB, source, destination, length) | `tiny_gea_packet_transmission_overhead` |
| Maximum ERD payload | 248 = 255 − 7 | `tiny_gea_packet_max_payload_length` |
| ERD cache entry | 248; entries with a larger `data_size` are rejected | `ERD_CACHE_MAX_DATA_SIZE` (`erd_cache.h`) |
| Write payload | 248 | `ERD_WRITE_MAX_DATA_SIZE` (`erd_write_bridge.h`) |
| UART receive buffer | 255 = 248 + 7 | `protocol_stack.h` |

`geappliances_bridge_bridge_init.cpp` pins the cache and write limits together with `static_assert(ERD_WRITE_MAX_DATA_SIZE == ERD_CACHE_MAX_DATA_SIZE)`, so those two cannot drift; nothing asserts the on-wire limit against them. Raising any layer requires confirming the appliance protocol supports a larger payload. See `docs/adr/0004-erd-cache-fixed-arena.md`.

## Request Retry Budgets

The ERD clients, not the bridges, own per-request retries (`protocol_stack.cpp`):

| Client | `request_timeout` | `request_retries` | Attempts per read | Worst case |
|---|---|---|---|---|
| GEA3 | 250 ms | 10 | 11 | 2.75 s |
| GEA2 | 250 ms | 1 | 2 | 500 ms |

The GEA3 budget exists to outlast slow first-access NVRAM lookups; the GEA2 budget covers transient bus collisions at 19200 baud without bridge-level retry logic. Behavior and failure signaling are specified in `docs/spec/protocol_stack.md`.

## Submodule Boundary

The GEA protocol stack implementation lives in the `lib/tiny` and `lib/tiny-gea-api` submodules. The bridge component interacts with them through well-defined C interfaces:

| Interface | Purpose |
|---|---|
| `i_tiny_uart_t` | UART HAL abstraction |
| `i_tiny_gea3_erd_client_t` | GEA3 ERD operations |
| `i_tiny_gea2_erd_client_t` | GEA2 ERD operations |
| `i_tiny_time_source_t` | Monotonic clock |

For protocol details, see the [tiny-gea-api](https://github.com/geappliances/tiny-gea-api) repository.