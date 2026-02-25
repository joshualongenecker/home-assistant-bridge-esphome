# GEA2 Protocol Implementation Guide

## Overview

This document describes the GEA2 (General Electric Appliance Protocol version 2) implementation for ESPHome. GEA2 is an older, slower protocol (19200 baud) used by legacy GE appliances that only supports polling—unlike GEA3 which supports both polling and subscription.

## Key Differences from Reference Implementation

### Architecture

**Reference Implementation** ([paulgoodjohn/home-assistant-adapter](https://github.com/paulgoodjohn/home-assistant-adapter)):
- Standalone ESP32-C3 application using PlatformIO
- Direct MQTT client implementation
- Hardcoded configuration in `Config.h`
- Single-purpose: GEA2 only

**This Implementation**:
- ESPHome component integrated into larger ESPHome framework
- Leverages ESPHome's existing MQTT infrastructure
- YAML-based configuration via ESPHome
- **Dual protocol**: Supports GEA3 and GEA2 simultaneously or independently
- Both protocols can run on separate UARTs concurrently

### Configuration

**Reference**: C++ header file configuration
```cpp
#define WIFI_SSID "..."
#define MQTT_BROKER "..."
#define DEVICE_ID "MyDevice"
```

**This Implementation**: YAML configuration
```yaml
geappliances_bridge:
  gea2_uart_id: gea2_uart
  gea2_device_id: "MyDevice"      # Optional
  gea2_polling_interval: 3000
  gea2_address: 0xA0              # Optional preferred address
```

### State Machine Design

**Reference**: Simple procedural loop
- Direct function calls
- Manual state tracking with flags
- Less structured state transitions

**This Implementation**: Hierarchical State Machine (HSM)
- Uses `tiny_hsm` library for formal state machine
- States:
  1. `state_identify_appliance` - Broadcast discovery
  2. `state_add_common_erds` - Poll common ERDs
  3. `state_add_energy_erds` - Poll energy ERDs
  4. `state_add_appliance_erds` - Poll appliance-specific ERDs
  5. `state_poll_erds_from_list` - Continuous polling
- Signal-driven transitions (timer_expired, read_completed, read_failed, etc.)
- More maintainable and testable

### Appliance Discovery

**Reference**: Queries specific address (0xA0 by default)
- Directly reads ERD 0x0008 from hardcoded address
- Single-device assumption

**This Implementation**: Broadcast-based discovery
- Sends ERD 0x0008 query to broadcast address `0xFF`
- **Logs all responding appliances** with debug messages
- Supports multiple appliances on same bus
- Preferentially selects `gea2_address` if specified and responds
- Falls back to first responder otherwise

Example log output:
```
[D][gea2_mqtt_bridge:xxx]: Appliance responded from GEA address: 0xA0
[D][gea2_mqtt_bridge:xxx]: Appliance responded from GEA address: 0xB0
[I][gea2_mqtt_bridge:xxx]: Locked to appliance at GEA address: 0xA0
```

### ERD List Management

**Reference**: Uses fixed appliance family groupings
- Directly includes appliance-specific ERD arrays
- Simpler lookup

**This Implementation**: Abstraction layer via `gea2_appliance_erds`
- Reuses existing `erd_lists.h` from GEA3 implementation
- Wrapper functions map appliance type byte to ERD lists
- Enables code reuse between GEA2 and GEA3
- Supports all 55 appliance types defined in `erd_lists.h`

### NVS (Non-Volatile Storage)

**Reference**: Direct ESP32 NVS API
```cpp
nvs_handle_t handle;
nvs_open("storage", NVS_READWRITE, &handle);
nvs_get_blob(handle, "poll_list", ...);
```

**This Implementation**: ESPHome Preferences API
```cpp
pref_ = global_preferences->make_preference<NVSData>(fnv1_hash("gea2_poll"));
pref_.load(&data);
pref_.save(&data);
```
- Higher-level abstraction
- Automatic namespace management
- Type-safe serialization

### Logging and Debugging

**Reference**: Basic Serial.printf debugging
- Limited structured logging

**This Implementation**: Comprehensive debug logging
- ESPHome logging macros (`ESP_LOGD`, `ESP_LOGI`, `ESP_LOGW`)
- **Raw bus command logging**: All TX/RX with Src/Dst/ERD
- Appliance discovery logging
- State transition logging
- Failure reason logging

Example debug output:
```
[D][gea2_mqtt_bridge:xxx]: GEA2 TX: Src=0xE4 Dst=0xFF ERD=0x0008 (READ_REQUEST - BROADCAST)
[D][gea2_mqtt_bridge:xxx]: GEA2 RX: Src=0xA0 Dst=0xE4 ERD=0x0008 Size=1 (READ_RESPONSE)
[D][gea2_mqtt_bridge:xxx]: GEA2 TX: Src=0xE4 Dst=0xA0 ERD=0x0001 (READ_REQUEST - DISCOVERY)
```

### Integration Architecture

**Reference**: Standalone application
- Main loop handles everything
- Direct hardware control

**This Implementation**: Component-based
- Integrates with ESPHome component lifecycle
  - `setup()` - Initialize components
  - `loop()` - Called every cycle
  - `dump_config()` - Display configuration
- Shares UART with other ESPHome components
- **Can coexist with GEA3 on separate UART**

## Implementation Details

### State Machine Flow

```
┌─────────────────────────────────────────────────────┐
│                    state_top                        │
│          (Parent state - handles signals)           │
└─────────────────────────────────────────────────────┘
                           │
           ┌───────────────┴───────────────┬──────────────────┬─────────────────┐
           │                               │                  │                 │
           ▼                               ▼                  ▼                 ▼
┌──────────────────────┐      ┌──────────────────────┐ ┌────────────────┐ ┌──────────────────┐
│state_identify_       │      │state_add_common_erds │ │state_add_energy│ │state_add_        │
│appliance             │─────▶│                      │─│_erds           │─│appliance_erds    │
│                      │      │                      │ │                │ │                  │
│• Broadcast to 0xFF   │      │• Poll common ERD list│ │• Poll energy   │ │• Poll appliance  │
│• Wait for responses  │      │• Add responding ERDs │ │  ERD list      │ │  specific list   │
│• Log all appliances  │      │  to poll list        │ │• Add responding│ │• Add responding  │
│• Lock to first/pref  │      │                      │ │  ERDs          │ │  ERDs            │
└──────────────────────┘      └──────────────────────┘ └────────────────┘ └──────────────────┘
                                                                                    │
                                                                                    │
                                                                                    ▼
                                                                          ┌─────────────────────┐
                                                                          │state_poll_erds_     │
                                                                          │from_list            │
                                                                          │                     │
                                                                          │• Continuous polling │
                                                                          │• Publish to MQTT    │
                                                                          │• Handle writes      │
                                                                          │• Detect lost device │
                                                                          └─────────────────────┘
```

### ERD Discovery Process

1. **Broadcast Discovery** (`state_identify_appliance`)
   - Send ERD 0x0008 (Appliance Type) to address 0xFF
   - Wait for responses (up to 3 seconds)
   - Log each responding appliance address
   - Select appliance:
     - If configured `gea2_address` responds → use it
     - Otherwise → use first responder
   - Extract appliance type byte from response

2. **Common ERDs** (`state_add_common_erds`)
   - Poll all ERDs in common list (see `erd_lists.h`)
   - Typically ~15-20 ERDs
   - Add responding ERDs to polling list

3. **Energy ERDs** (`state_add_energy_erds`)
   - Poll energy-related ERDs
   - Typically power consumption, runtime metrics
   - Add responding ERDs to polling list

4. **Appliance-Specific ERDs** (`state_add_appliance_erds`)
   - Look up ERD list based on appliance type byte
   - Poll all ERDs in appliance-specific list
   - Can be 50-200+ ERDs depending on appliance
   - Add responding ERDs to polling list

5. **Save to NVS**
   - Store discovered ERD list
   - Store appliance address
   - Used on next boot to skip discovery

6. **Continuous Polling** (`state_poll_erds_from_list`)
   - Poll each ERD in discovered list
   - Default interval: 3 seconds per ERD
   - Publish value changes to MQTT
   - Reset "appliance lost" timer on each successful read

### Appliance Lost Detection

- 60-second timeout timer
- Reset on every successful ERD read
- If timer expires:
  - Clear NVS storage
  - Restart from broadcast discovery
  - Log: "Appliance connection lost, rediscovering..."

### Bus Communication Protocol

#### GEA2 Packet Structure
```
┌────────┬────────┬─────────┬──────────┬──────────┬─────┐
│ Dest   │ Length │ Src     │ ERD Hi   │ ERD Lo   │ ... │
│ (1B)   │ (1B)   │ (1B)    │ (1B)     │ (1B)     │     │
└────────┴────────┴─────────┴──────────┴──────────┴─────┘
```

#### Address Scheme
- **Board/Client Address**: `0xE4` (fixed)
- **Broadcast Address**: `0xFF` (discovery only)
- **Appliance Address**: Typically `0xA0`, but auto-detected

#### Operation Types
- **READ_REQUEST**: Query ERD value
- **READ_RESPONSE**: ERD value response
- **WRITE_REQUEST**: Set ERD value
- **WRITE_RESPONSE**: Write acknowledgment

### Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `gea2_uart_id` | UART ID | Required | UART component for GEA2 bus |
| `gea2_device_id` | String | Auto-generated | MQTT device ID |
| `gea2_polling_interval` | Integer | 3000 | Polling interval (ms) |
| `gea2_address` | Hex | 0xA0 | Preferred appliance address |

### NVS Data Structure

```cpp
struct NVSData {
    uint16_t polling_list_count;      // Number of discovered ERDs
    uint8_t erd_host_address;         // Appliance address
    tiny_erd_t erd_polling_list[256]; // Discovered ERD list
};
```

Storage key: `fnv1_hash("gea2_poll")` for namespace uniqueness

## Hardware Requirements

### UART Configuration

```yaml
uart:
  - id: gea2_uart
    tx_pin: GPIO10      # Adjust for your board
    rx_pin: GPIO9       # Adjust for your board
    baud_rate: 19200    # GEA2 standard baud rate
```

### Timing Requirements

- **1ms interrupt**: Required by `tiny_gea2_interface`
  - Implemented via ESPHome timer
  - Publishes to `tiny_event_t` for interface
- **Polling interval**: Configurable, default 3000ms
- **Discovery timeout**: 3 seconds per attempt
- **Lost appliance timeout**: 60 seconds

## Dependencies

### External Libraries
- `tiny_gea2_interface` - Low-level GEA2 protocol
- `tiny_gea2_erd_client` - ERD client abstraction
- `tiny_hsm` - Hierarchical state machine
- `tiny_timer` - Timer management
- `i_mqtt_client` - MQTT interface

### ESPHome Components
- `uart` - UART communication
- `mqtt` - MQTT connectivity
- `preferences` - NVS storage

## Testing and Debugging

### Enable Debug Logging

```yaml
logger:
  level: DEBUG
  logs:
    gea2_mqtt_bridge: DEBUG
```

### Expected Log Output

**Successful discovery:**
```
[I][gea2_mqtt_bridge:xxx]: GEA2 enabled, initializing...
[D][gea2_mqtt_bridge:xxx]: GEA2 TX: Src=0xE4 Dst=0xFF ERD=0x0008 (READ_REQUEST - BROADCAST)
[D][gea2_mqtt_bridge:xxx]: Appliance responded from GEA address: 0xA0
[I][gea2_mqtt_bridge:xxx]: Locked to appliance at GEA address: 0xA0
[D][gea2_mqtt_bridge:xxx]: Appliance type: 0x05 (Refrigerator)
[I][gea2_mqtt_bridge:xxx]: Discovering common ERDs...
[D][gea2_mqtt_bridge:xxx]: GEA2 TX: Src=0xE4 Dst=0xA0 ERD=0x0001 (READ_REQUEST - DISCOVERY)
[D][gea2_mqtt_bridge:xxx]: GEA2 RX: Src=0xA0 Dst=0xE4 ERD=0x0001 Size=1 (READ_RESPONSE)
...
[I][gea2_mqtt_bridge:xxx]: Discovery complete, polling 47 ERDs
```

**Loaded from NVS:**
```
[I][gea2_mqtt_bridge:xxx]: NV storage found and loaded
[I][gea2_mqtt_bridge:xxx]: Stored number of polled ERDs is 47
[I][gea2_mqtt_bridge:xxx]: GEA address set to 0xA0
[I][gea2_mqtt_bridge:xxx]: Starting continuous polling
```

## Troubleshooting

### No appliances respond to broadcast
- Check UART wiring (TX/RX not swapped)
- Verify baud rate is 19200
- Check appliance is powered on
- Verify RJ45 pinout matches appliance

### Discovery works but polling fails
- ERD may not be supported by appliance
- Check raw command logs for error codes
- Some ERDs are write-only

### NVS data becomes stale
- Appliance firmware updated
- Different appliance connected
- Solution: Clear NVS via 60-second timeout or reflash

### Multiple appliances detected
- Both will be logged
- First responder used by default
- Set `gea2_address` to prefer specific appliance

## Performance Characteristics

### Discovery Time
- **Cold start** (no NVS): 30-120 seconds depending on appliance
  - Broadcast: 3 seconds
  - Common ERDs: ~45 seconds (15 ERDs × 3s)
  - Energy ERDs: ~30 seconds (10 ERDs × 3s)
  - Appliance ERDs: varies (20-100+ ERDs)
- **Warm start** (NVS loaded): < 1 second

### Polling Rate
- Default: 3000ms per ERD
- 47 ERDs → ~141 seconds per complete cycle
- Configurable via `gea2_polling_interval`

### Memory Usage
- State machine: ~1KB
- Polling list: ~512 bytes (256 ERDs × 2 bytes)
- NVS storage: ~520 bytes
- Total: ~2KB RAM, ~520 bytes flash

## Future Enhancements

Potential improvements not in current implementation:

1. **Dynamic polling intervals** - Poll frequently-changing ERDs faster
2. **Write queue management** - Better handling of multiple write requests
3. **Multi-appliance support** - Poll multiple GEA2 appliances simultaneously
4. **ERD aliasing** - Merge duplicate ERDs across appliances
5. **Statistical logging** - Track success rates, response times

## References

- Reference Implementation: [paulgoodjohn/home-assistant-adapter](https://github.com/paulgoodjohn/home-assistant-adapter)
- GEA3 Implementation: [geappliances/home-assistant-bridge](https://github.com/geappliances/home-assistant-bridge)
- ESPHome Documentation: [esphome.io](https://esphome.io)
- Hardware: [FirstBuild Home Assistant Adapter](https://firstbuild.com/inventions/home-assistant-adapter/)
