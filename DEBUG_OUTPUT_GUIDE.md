# Debug Output Guide

This guide explains the debug outputs added to help troubleshoot MQTT ERD publishing issues.

## Overview

Debug logging has been added throughout the MQTT bridge to help identify where ERD publications might be getting stuck. The logging is controlled by ESPHome's log level configuration and only applies when running as an ESPHome component.

## Configuring Debug Level

Add to your ESPHome YAML configuration:

```yaml
logger:
  level: DEBUG  # or INFO, or VERBOSE
  logs:
    geappliances_bridge: DEBUG
    geappliances_bridge.mqtt: DEBUG
    geappliances_bridge.mqtt_bridge: DEBUG
    geappliances_bridge.uart: VERBOSE  # Use VERBOSE for detailed UART traffic
```

## Debug Output Locations

### 1. MQTT Bridge State Machine (`geappliances_bridge.mqtt_bridge`)

**Subscribing State:**
- `[I][geappliances_bridge.mqtt_bridge:XXX]: Entering subscribing state` - Bridge is attempting to subscribe to ERD updates
- `[D][geappliances_bridge.mqtt_bridge:XXX]: Subscribe request sent, waiting for response...` - Subscription request has been queued
- `[I][geappliances_bridge.mqtt_bridge:XXX]: Subscribe failed to queue, retrying in 1000 ms` - Queue full, will retry
- `[I][geappliances_bridge.mqtt_bridge:XXX]: Subscription failed, retrying...` - Subscription attempt failed
- `[I][geappliances_bridge.mqtt_bridge:XXX]: Subscription added or retained successfully` - Subscription succeeded

**Subscribed State:**
- `[I][geappliances_bridge.mqtt_bridge:XXX]: Entering subscribed state` - Bridge successfully subscribed, ready to receive ERD updates
- `[D][geappliances_bridge.mqtt_bridge:XXX]: Timer expired in subscribed state, retaining subscription` - Periodic keep-alive
- `[I][geappliances_bridge.mqtt_bridge:XXX]: Subscription host came online, resubscribing...` - Appliance came online
- `[I][geappliances_bridge.mqtt_bridge:XXX]: MQTT disconnected, transitioning to subscribing state` - MQTT disconnected

**ERD Activity:**
- `[D][geappliances_bridge.mqtt_bridge:XXX]: ERD client activity: subscription_added_or_retained` - Subscription confirmed
- `[D][geappliances_bridge.mqtt_bridge:XXX]: ERD client activity: subscription_publication_received for ERD 0xXXXX` - ERD data received from appliance
- `[D][geappliances_bridge.mqtt_bridge:XXX]: Received publication for ERD 0xXXXX (size: N)` - Processing received ERD data
- `[I][geappliances_bridge.mqtt_bridge:XXX]: Registering new ERD 0xXXXX` - First time seeing this ERD
- `[D][geappliances_bridge.mqtt_bridge:XXX]: Updating ERD 0xXXXX with N bytes` - Updating ERD value
- `[I][geappliances_bridge.mqtt_bridge:XXX]: ERD client activity: subscribe_failed` - Subscription attempt failed
- `[I][geappliances_bridge.mqtt_bridge:XXX]: ERD client activity: subscription_host_came_online` - Host appliance came online

**Write Operations:**
- `[I][geappliances_bridge.mqtt_bridge:XXX]: Write requested for ERD 0xXXXX (size: N)` - Write request received
- `[I][geappliances_bridge.mqtt_bridge:XXX]: ERD client activity: write_completed for ERD 0xXXXX` - Write succeeded
- `[I][geappliances_bridge.mqtt_bridge:XXX]: ERD client activity: write_failed for ERD 0xXXXX (reason: N)` - Write failed

### 2. MQTT Client Adapter (`geappliances_bridge.mqtt`)

**Initialization:**
- `[I][geappliances_bridge.mqtt:XXX]: MQTT client adapter initialized for device: <device_id>` - MQTT adapter initialized

**ERD Registration:**
- `[I][geappliances_bridge.mqtt:XXX]: Registering ERD 0xXXXX - value_topic: <topic>, write_topic: <topic>` - New ERD discovered
- `[D][geappliances_bridge.mqtt:XXX]: MQTT connected, subscribing to write topic for ERD 0xXXXX` - Subscribing to write commands
- `[W][geappliances_bridge.mqtt:XXX]: MQTT not connected, cannot subscribe to write topic for ERD 0xXXXX` - MQTT disconnected
- `[E][geappliances_bridge.mqtt:XXX]: MQTT client is null, cannot register ERD 0xXXXX` - MQTT client not available

**ERD Updates:**
- `[I][geappliances_bridge.mqtt:XXX]: Updating ERD 0xXXXX: topic=<topic>, payload=<hex> (size=N)` - Publishing ERD value
- `[D][geappliances_bridge.mqtt:XXX]: MQTT connected, publishing ERD 0xXXXX update` - Attempting to publish
- `[D][geappliances_bridge.mqtt:XXX]: Published ERD 0xXXXX update successfully` - Publish succeeded
- `[W][geappliances_bridge.mqtt:XXX]: MQTT not connected, cannot publish ERD 0xXXXX update` - MQTT disconnected
- `[E][geappliances_bridge.mqtt:XXX]: MQTT client is null, cannot publish ERD 0xXXXX update` - MQTT client not available

**Connection Events:**
- `[I][geappliances_bridge.mqtt:XXX]: MQTT client adapter notifying disconnection` - MQTT disconnection detected

### 3. Main Bridge Component (`geappliances_bridge`)

**Setup:**
- `[D][geappliances_bridge:XXX]: Initializing timer group` - Timer system starting
- `[D][geappliances_bridge:XXX]: Initializing UART adapter with baud rate 230400` - UART starting
- `[D][geappliances_bridge:XXX]: Initializing MQTT client adapter for device: <device_id>` - MQTT adapter starting
- `[D][geappliances_bridge:XXX]: Initializing uptime monitor` - Uptime monitor starting
- `[D][geappliances_bridge:XXX]: Initializing GEA3 interface with client address 0xXX` - GEA3 protocol starting
- `[D][geappliances_bridge:XXX]: Initializing ERD client` - ERD client starting
- `[D][geappliances_bridge:XXX]: Initializing MQTT bridge` - MQTT bridge starting

**Runtime:**
- `[I][geappliances_bridge:XXX]: MQTT connection established` - MQTT connected
- `[W][geappliances_bridge:XXX]: MQTT connection lost` - MQTT disconnected
- `[I][geappliances_bridge:XXX]: MQTT connected, notifying bridge to reset subscriptions` - Reconnection handling

### 4. UART Adapter (`geappliances_bridge.uart`)

**Note:** UART logging uses VERBOSE level to avoid spam. Enable only when debugging UART issues.

**Initialization:**
- `[I][geappliances_bridge.uart:XXX]: UART adapter initialized` - UART adapter ready

**Data Transfer (VERBOSE level):**
- `[VV][geappliances_bridge.uart:XXX]: Received N bytes (total: M)` - Data received from appliance
- `[VV][geappliances_bridge.uart:XXX]: Sent byte 0xXX (total: M)` - Data sent to appliance

## Troubleshooting Common Issues

### Issue: Uptime published but no ERD updates

**Expected Log Pattern:**
1. Bridge enters "subscribing state"
2. Subscribe request sent
3. Subscription added or retained successfully
4. Bridge enters "subscribed state"
5. ERD publications received and published

**Check for:**
- Does the log show "Entering subscribed state"? If not, subscription is failing
- Does the log show "ERD client activity: subscription_publication_received"? If not, appliance is not sending ERD data
- Does the log show "Updating ERD" but not "Published ERD update successfully"? MQTT publish is failing

### Issue: Stuck in subscribing state

**Logs to check:**
```
[I][geappliances_bridge.mqtt_bridge:XXX]: Entering subscribing state
[D][geappliances_bridge.mqtt_bridge:XXX]: Subscribe request sent, waiting for response...
(no "Subscription added or retained successfully" follows)
```

**Possible causes:**
- Appliance is not responding to subscription requests
- UART connection issues
- Wrong client address configured

**Enable VERBOSE logging on UART to verify communication:**
```yaml
logger:
  logs:
    geappliances_bridge.uart: VERBOSE
```

### Issue: ERDs received but not published

**Logs to check:**
```
[D][geappliances_bridge.mqtt_bridge:XXX]: Received publication for ERD 0xXXXX (size: N)
[I][geappliances_bridge.mqtt_bridge:XXX]: Registering new ERD 0xXXXX
[I][geappliances_bridge.mqtt:XXX]: Updating ERD 0xXXXX: topic=...
[W][geappliances_bridge.mqtt:XXX]: MQTT not connected, cannot publish ERD 0xXXXX update
```

**Possible causes:**
- MQTT connection lost between receiving ERD and publishing
- MQTT broker unreachable
- MQTT credentials invalid

### Issue: MQTT client is null

**Logs to check:**
```
[E][geappliances_bridge.mqtt:XXX]: MQTT client is null, cannot register ERD 0xXXXX
```

**Possible cause:**
- MQTT component not configured in ESPHome YAML
- Component initialization order issue

**Solution:**
Ensure MQTT is configured before geappliances_bridge:
```yaml
mqtt:
  broker: ...
  
geappliances_bridge:
  ...
```

## Reading the Logs

When monitoring serial output, look for this sequence for successful ERD publishing:

1. **Setup phase:**
   ```
   [D][geappliances_bridge:XXX]: Initializing ...
   [I][geappliances_bridge.mqtt:XXX]: MQTT client adapter initialized for device: ...
   [I][geappliances_bridge.uart:XXX]: UART adapter initialized
   ```

2. **Subscription phase:**
   ```
   [I][geappliances_bridge.mqtt_bridge:XXX]: Entering subscribing state
   [D][geappliances_bridge.mqtt_bridge:XXX]: Subscribe request sent, waiting for response...
   [I][geappliances_bridge.mqtt_bridge:XXX]: Subscription added or retained successfully
   [I][geappliances_bridge.mqtt_bridge:XXX]: Entering subscribed state
   ```

3. **ERD reception and publishing phase:**
   ```
   [D][geappliances_bridge.mqtt_bridge:XXX]: ERD client activity: subscription_publication_received for ERD 0xXXXX
   [D][geappliances_bridge.mqtt_bridge:XXX]: Received publication for ERD 0xXXXX (size: N)
   [I][geappliances_bridge.mqtt_bridge:XXX]: Registering new ERD 0xXXXX
   [I][geappliances_bridge.mqtt:XXX]: Registering ERD 0xXXXX - value_topic: geappliances/.../erd/0xXXXX/value
   [I][geappliances_bridge.mqtt:XXX]: Updating ERD 0xXXXX: topic=..., payload=... (size=N)
   [D][geappliances_bridge.mqtt:XXX]: Published ERD 0xXXXX update successfully
   ```

If you see steps 1 and 2 but not step 3, the issue is with ERD data reception from the appliance. If you see step 3 without "Published ... successfully", the issue is with MQTT publishing.
