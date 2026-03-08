# Startup Sequences

This document describes the complete startup sequence for the GE Appliances Bridge component, covering all configuration variations.

## Overview

The bridge follows a unified startup sequence regardless of configuration. All configurations wait for autodiscovery to complete before initializing the MQTT bridge, ensuring the appliance address and protocol are known before communication begins.

## Timing Constants

- **STARTUP_DELAY_MS**: 5 seconds - Wait time after MQTT connects before starting autodiscovery
- **AUTODISCOVERY_BROADCAST_WINDOW_MS**: 5 seconds - Time window to collect responses from broadcast
- **GEA2_LOOP_DURATION_MS**: 200 milliseconds - Tight loop duration for GEA2 operations
- **SUBSCRIPTION_TIMEOUT_MS**: 30 seconds - Fallback timeout for AUTO mode

---

## Unified Startup Flowchart

```
┌─────────────────────────────────────────────────────────────────────┐
│ Phase 1: Hardware Initialization (setup())                          │
│                                                                      │
│ • Initialize timer group                                            │
│ • Initialize UART adapter(s) for configured protocol(s):            │
│   - GEA3: interface + ERD client                                    │
│   - GEA2: interface + 1ms timer + ERD client + adapter              │
│ • Subscribe to activity events                                      │
│ • Log device ID (if configured) - will apply after autodiscovery    │
│ • Set device_id_state = IDLE                                        │
│ • Set autodiscovery_state = WAITING_FOR_MQTT                        │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────────┐
│ Phase 2: MQTT Connection Wait                                       │
│                                                                      │
│ • loop() polls for MQTT broker connection                           │
│ • When connected: on_mqtt_connected_() triggered                    │
│ • Set autodiscovery_state = WAITING_5S                              │
│ • Start 5-second timer                                              │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────────┐
│ Phase 3: Autodiscovery (Protocol & Address Detection)               │
│                                                                      │
│ After 5s delay, send broadcast to detect appliance:                 │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
                    ┌─────────────────┐
                    │ Which UARTs are │
                    │  configured?    │
                    └─────────────────┘
                              ↓
        ┌─────────────────────┼─────────────────────┐
        ↓                     ↓                     ↓
   ┌─────────┐         ┌─────────────┐       ┌─────────┐
   │GEA3 only│         │  Both GEA3  │       │GEA2 only│
   │         │         │  and GEA2   │       │         │
   └─────────┘         └─────────────┘       └─────────┘
        ↓                     ↓                     ↓
  ┌──────────┐         ┌──────────┐          ┌──────────┐
  │Try GEA3  │         │Try GEA3  │          │Try GEA2  │
  │broadcast │         │broadcast │          │broadcast │
  └──────────┘         └──────────┘          └──────────┘
        ↓                     ↓                     ↓
  ┌──────────┐         ┌──────────┐          ┌──────────┐
      │Wait 5s   │         │Wait 5s   │          │Wait 5s   │
  │for reply │         │for reply │          │for reply │
  └──────────┘         └──────────┘          └──────────┘
        ↓                     ↓                     ↓
   Response?            Response?             Response?
     Yes │                 Yes │ No              Yes │
         │                     ↓                     │
         │             ┌──────────┐                  │
         │             │Try GEA2  │                  │
         │             │broadcast │                  │
         │             └──────────┘                  │
         │                     ↓                     │
         │              ┌──────────┐                 │
         │              │Wait 5s   │                 │
         │              │for reply │                 │
         │              └──────────┘                 │
         │                     ↓                     │
         │                 Response?                 │
         │                   Yes │                   │
         └─────────────────────┼───────────────────┘
                               ↓
                ┌──────────────────────────────┐
                │  Appliance Discovered!       │
                │                              │
                │ • Extract host_address       │
                │ • Set active_erd_client      │
                │ • Set gea2_protocol_active   │
                │   (if GEA2 used)             │
                │ • autodiscovery_state =      │
                │   COMPLETE                   │
                └──────────────────────────────┘
                               ↓
┌─────────────────────────────────────────────────────────────────────┐
│ Phase 4: Device ID Determination                                    │
│                                                                      │
│ start_device_id_generation_() called                                │
└─────────────────────────────────────────────────────────────────────┘
                               ↓
                    ┌──────────────────┐
                    │ Device ID        │
                    │ configured?      │
                    └──────────────────┘
                               ↓
                        Yes ───┼─── No
                               │
        ┌──────────────────────┼──────────────────────┐
        ↓                                             ↓
┌─────────────────┐                    ┌─────────────────────────┐
│ Use Configured  │                    │ Generate from ERDs:     │
│                 │                    │                         │
│ • final_device_ │                    │ 1. Read ERD 0x0008      │
│   id = config   │                    │    (appliance type)     │
│ • device_id_    │                    │ 2. Read ERD 0x0001      │
│   state =       │                    │    (model number)       │
│   COMPLETE      │                    │ 3. Read ERD 0x0002      │
│ • bridge_init_  │                    │    (serial number)      │
│   state =       │                    │ 4. Generate ID:         │
│   WAITING_FOR_  │                    │    {type}_{model}_      │
│   MQTT          │                    │    {serial}             │
└─────────────────┘                    │ • device_id_state =     │
                                       │   COMPLETE              │
                                       │ • bridge_init_state =   │
                                       │   WAITING_FOR_MQTT      │
                                       └─────────────────────────┘
        │                                             │
        └──────────────────────┬──────────────────────┘
                               ↓
┌─────────────────────────────────────────────────────────────────────┐
│ Phase 5: Bridge Initialization                                      │
│                                                                      │
│ Conditions checked in loop():                                       │
│ • device_id_state == COMPLETE                                       │
│ • autodiscovery_state == COMPLETE                                   │
│ • MQTT is connected                                                 │
└─────────────────────────────────────────────────────────────────────┘
                               ↓
                    ┌──────────────────┐
                    │ initialize_mqtt_ │
                    │ bridge_()        │
                    └──────────────────┘
                               ↓
                    ┌──────────────────┐
                    │ Which protocol   │
                    │ was discovered?  │
                    └──────────────────┘
                               ↓
                        GEA2 ──┼── GEA3
                               │
        ┌──────────────────────┼───────────────────────┐
        ↓                                              ↓
┌─────────────────┐                    ┌──────────────────────┐
│ GEA2 Mode       │                    │ GEA3 Mode            │
│                 │                    │                      │
│ • Force polling │                    │ • Use configured mode│
│   (no subscribe │                    │   (default: AUTO)    │
│   support)      │                    │ • Try subscription   │
│ • Init mqtt_    │                    │   first              │
│   bridge_       │                    │ • Init mqtt_bridge_  │
│   polling_      │                    │ • If AUTO: fallback  │
│                 │                    │   to polling after   │
│                 │                    │   30s if no activity │
└─────────────────┘                    └──────────────────────┘
        │                                              │
        └──────────────────────┬───────────────────────┘
                               ↓
                ┌──────────────────────────────┐
                │ • mqtt_bridge_initialized =  │
                │   true                       │
                │ • bridge_init_state =        │
                │   COMPLETE                   │
                └──────────────────────────────┘
                               ↓
┌─────────────────────────────────────────────────────────────────────┐
│ Phase 6: Normal Operation                                           │
│                                                                      │
│ loop() continuously runs:                                           │
│ • tiny_timer_group_run()                                            │
│ • GEA3: tiny_gea3_interface_run() (standard ~50ms loop)             │
│ • GEA2: 200ms tight loop of tiny_gea2_interface_run()               │
│ • MQTT bridge handles ERD communication                             │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Configuration Variations

The bridge behavior adapts based on YAML configuration:

### UART Configuration
- **GEA3 only**: Tries GEA3 repeatedly until appliance found
- **GEA2 only**: Tries GEA2 repeatedly until appliance found
- **Both**: Tries GEA3 first, falls back to GEA2, alternates until appliance found

### Device ID Configuration
- **Not set**: Generated from appliance ERDs (type, model, serial)
- **Set**: Applied after autodiscovery completes

### Mode Configuration
- **POLL**: Always uses polling (queries ERDs at interval)
- **SUBSCRIBE**: Always uses subscriptions (appliance pushes updates)
- **AUTO** (default): Tries subscription first, falls back to polling after 30s if no activity

**Note:** GEA2 always uses polling mode (subscriptions not supported by protocol)

---

## Timing Estimates

| Configuration | Protocol Detection | Device ID | Total Time |
|---------------|-------------------|-----------|------------|
| GEA3 + Auto ID | ~5s (GEA3 found) | ~5-15s (3 ERD reads) | ~15-25s |
| GEA2 + Auto ID | ~5s (GEA2 found) | ~5-15s (3 ERD reads) | ~15-25s |
| GEA3 + Fixed ID | ~5s (GEA3 found) | 0s (configured) | ~10s |
| GEA2 + Fixed ID | ~5s (GEA2 found) | 0s (configured) | ~10s |
| Both + Auto ID (GEA3) | ~5s (GEA3 found) | ~5-15s | ~15-25s |
| Both + Auto ID (GEA2) | ~10s (GEA3 fails, GEA2 found) | ~5-15s | ~20-30s |
| Both + Fixed ID (GEA3) | ~5s (GEA3 found) | 0s | ~10s |
| Both + Fixed ID (GEA2) | ~10s (GEA3 fails, GEA2 found) | 0s | ~15s |

**Breakdown:**
- MQTT connection: Variable (depends on network)
- Initial delay: 5 seconds (STARTUP_DELAY_MS)
- Protocol broadcast: 5 seconds per protocol (AUTODISCOVERY_BROADCAST_WINDOW_MS)
- Device ID generation: 5-15 seconds for 3 ERD reads (if needed)
- Bridge initialization: <1 second

---

## Key Behaviors

### Autodiscovery Always Runs
Even with a configured device ID, autodiscovery runs to detect:
- Appliance address on the bus
- Which protocol the appliance uses (when both UARTs configured)

### Device ID Applied After Autodiscovery
Both configured and autogenerated device IDs are only applied after autodiscovery completes. This ensures:
- The correct appliance address is known
- The correct protocol is selected
- The bridge can communicate reliably from the start

### GEA2 Special Handling
GEA2 protocol requires special timing due to slower baud rate (19200 vs 230400):
- Uses 200ms tight loop for all operations
- Always uses polling mode (no subscription support)
- Longer response times for large ERDs

### Bridge Initialization Conditions
The MQTT bridge only initializes when **all three** conditions are met:
1. Device ID is ready (configured or generated)
2. Autodiscovery has completed
3. MQTT broker is connected

---

## Troubleshooting

### Startup Takes Longer Than Expected

**Check:**
- MQTT broker connectivity (first bottleneck)
- Appliance is powered on and responsive
- Correct UART pins configured
- Appliance protocol matches configuration

### Autodiscovery Keeps Retrying

**Symptoms:** Logs show repeated broadcasts, never finds appliance

**Possible causes:**
- Wrong UART pins
- Appliance not powered or not on bus
- Wrong protocol configured (try configuring both)
- Baud rate incorrect (should be 230400 for GEA3, 19200 for GEA2)

### Device ID Generation Fails

**Symptoms:** Repeated ERD read attempts for 0x0001, 0x0002, 0x0008

**Possible causes:**
- Appliance doesn't support these ERDs
- Communication timing issues
- Fix: Configure a device_id in YAML to bypass generation

### Bridge Initializes But No Data

**Symptoms:** Bridge starts but no ERD values in MQTT

**Possible causes:**
- Subscription mode not working (AUTO mode will fall back after 30s)
- Wrong appliance address detected
- Check logs for ERD client activity
