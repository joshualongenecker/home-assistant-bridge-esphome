# Troubleshooting

Diagnostic flow for common issues with the GE Appliances Bridge.

## No Communication with Appliance

### Symptoms

- Logs show repeated autodiscovery attempts with no response.
- Device never appears in Home Assistant.

### Diagnostic Steps

1. **Check physical connection**
   - Ensure the RJ45 cable is firmly seated on both the adapter and the appliance.
   - Try a different Ethernet cable.

2. **Verify appliance compatibility**
   - Confirm the appliance has a GEA3 (or GEA2) serial port.
   - Check the appliance is powered on and not in a diagnostic mode.

3. **Check UART configuration**
   - GEA3: `baud_rate: 230400`, pins GPIO21/GPIO20 on the Xiao ESP32-C3.
   - GEA2: `baud_rate: 19200`, with `rx_full_threshold: 1` and `rx_timeout: 1`.

4. **Enable debug logging**
   ```yaml
   logger:
     level: DEBUG
   ```
   Look for:
   - `Autodiscovery broadcast sent` — confirms the bridge is sending.
   - `Board found at address 0xXX` — confirms the appliance responded.

5. **Try GEA2 fallback**
   If GEA3 gets no response, configure both UARTs and let autodiscovery try GEA2:
   ```yaml
   geappliances_bridge:
     gea3_uart_id: gea3_uart
     gea2_uart_id: gea2_uart
   ```

## Device Appears but Entities Are Missing

### Symptoms

- Device shows in Home Assistant but has few or no entities.
- MQTT topics exist but discovery topics are sparse.

### Diagnostic Steps

1. **Check `filter_config_topics`**
   - Default is `true`, which filters ~19% of entities (firmware metadata, commissioning state, fault data).
   - Set to `false` to see all entities: `filter_config_topics: false`.

2. **Check `appliance_api_parsing`**
   - If set to `true`, only ERDs the appliance reports as supported are exposed.
   - Set to `false` to expose all known ERDs (may include unsupported ones that return errors).

3. **Trigger discovery refresh**
   - Press the **Discovery Refresh** button in Home Assistant (if enabled).
   - This queues a cleanup of stale discovery topics, republishes fresh ones, and reboots the device.
   - The request is queued if pressed before the bridge is ready — it will execute once steady state, MQTT, and device ID are available.

4. **Check MQTT connectivity**
   - Verify the bridge is connected to MQTT: look for `MQTT connected` in logs.
   - Check that discovery topics are being published: `mosquitto_sub -t 'homeassistant/#' -v`.

## Polling Mode Not Working

### Symptoms

- Mode is set to `poll` but no data appears.
- Or mode is `auto` and it falls back to poll but still shows nothing.

### Diagnostic Steps

1. **Verify polling interval**
   - Default is 10000 ms (10 seconds). First data appears after the first poll cycle.
   - Check logs for `Polling bridge: starting poll cycle`.

2. **Check ERD verification**
   - During the first poll cycle, the bridge probes each ERD to verify it responds.
   - ERDs that don't respond are excluded from future polling.
   - Look for `Probe ERD 0xXXXX: not supported` in debug logs.

3. **Try with `appliance_api_parsing: false`**
   - This polls all known ERDs instead of only the ones the appliance reports as supported.
   - Useful if the feature bit ERDs are not responding correctly.

## MQTT Connection Issues

### Symptoms

- Bridge connects to WiFi but not MQTT.
- Entities disappear after a reboot and do not come back.

### Diagnostic Steps

1. **Verify MQTT broker address and credentials** in `secrets.yaml`.

2. **Check broker accessibility** from the adapter's network:
   ```bash
   # From a machine on the same network:
   mosquitto_sub -h <broker> -u <user> -P <pass> -t '#' -v
   ```

3. **Check ESPHome MQTT configuration:**
   ```yaml
   mqtt:
     broker: !secret mqtt_broker
     username: !secret mqtt_username
     password: !secret mqtt_password
     port: 1883          # default, omit if using 1883
     discovery: true     # required for Home Assistant auto-discovery
     discovery_prefix: homeassistant
     clean_session: true
   ```

4. **Look for MQTT errors in logs:**
   - `MQTT connect failed` — wrong credentials or broker unreachable.
   - `MQTT disconnected` — network issue or broker restart.

5. **After firmware update, entities may briefly disappear:** After a firmware update, the bridge detects discovery data changes (hash or device ID) at steady state, cleans old discovery topics, publishes fresh ones, and reboots. During this cycle, entities may temporarily disappear from Home Assistant. They will reappear after the bridge completes the cleanup → publish → reboot cycle.

## Low Memory / WDT Reset

### Symptoms

- Device reboots unexpectedly.
- Logs show `Task watchdog timeout` or heap exhaustion.

### Diagnostic Steps

1. **Check heap usage** in ESPHome logs:
   - ESP32-C3 devices have only 320 KB SRAM. During HA discovery, heap can drop to ~6 KB.
   - ESP32-C6 devices have 512 KB SRAM and are less affected.

2. **Consider ESP32-C6** if running on C3 with a large appliance (dishwasher, range).

3. **Reduce entity count:**
   - Keep `filter_config_topics: true` (default).
   - Increase `throttle_rate_seconds` to reduce publish frequency.

4. **See [HARDWARE.md](../../HARDWARE.md)** for detailed memory constraints.

## Discovery Change Detection & Refresh

### Symptoms

- After a firmware update, entities briefly disappear from Home Assistant.
- The Discovery Refresh button does not seem to do anything immediately.

### Explanation

- **After firmware update:** The bridge detects discovery data changes (hash or device ID) at steady state, cleans old discovery topics, publishes fresh ones, and reboots. During this cycle, entities may temporarily disappear. They will reappear after the bridge completes the cleanup → publish → reboot cycle.
- **Discovery Refresh button:** When pressed, the request is queued. If the bridge is not yet ready (steady state, MQTT connected, device ID complete), the request waits until those conditions are met. Check logs for "Discovery refresh queued, will execute when appliance is ready".

### Diagnostic Steps

1. **Check logs for change detection:** Look for "Discovery state changed (hash=changed, device_id=same), cleaning old topics" or "Discovery state changed (hash=same, device_id=changed), cleaning old topics".
2. **Check logs for discovery refresh:** Look for "Discovery refresh queued, will execute when appliance is ready" followed by "HA discovery cleanup complete, publishing fresh discovery...".
3. **Check logs for fresh install:** Look for "Starting initial HA discovery publish..." on first boot.
4. **Wait for the cycle to complete:** The cleanup → publish → reboot cycle can take 1–2 minutes depending on the number of entities.


## Write Commands Fail

### Symptoms

- Writing to an ERD from Home Assistant returns `failure`.

### Diagnostic Steps

1. **Check the ERD is writable** — not all ERDs support writes. The appliance returns a "not supported" for read-only ERDs.

2. **Check payload format** — writes must be hex-encoded bytes matching the ERD's expected size.
   - Example: `01` for a 1-byte boolean ERD.

3. **Check Sensor Lock** — some diagnostic ERDs are only writable when Sensor Lock (ERD 0x7042) is enabled.

4. **Check for concurrent writes** — the write bridge processes one write at a time. Rapid successive writes may fail.

## General Debugging

### Enable Verbose Logging

```yaml
logger:
  level: DEBUG
  logs:
    geappliances_bridge: DEBUG
    mqtt: DEBUG
    uart: DEBUG
```

### Read the Boot Summary

At DEBUG the bridge prints one line per subsystem instead of one line per ERD. `Custom ERDs:` comes from `dump_config()` during setup; `Subscription:`, `Polled ERDs:` and `Feature bits:` come from the block that runs once, when the bridge first reaches steady state; `Discovery:` is printed per discovery run:

```text
[D][geappliances_bridge]:   Custom ERDs: 65 configured (primary 65)
[D][geappliances_bridge]:   Subscription: 151 ERDs (host board 0xC0)
[D][geappliances_bridge]:   Feature bits: 106 valid, 7 skipped (unsupported or unreadable)
[D][ha_discovery_manager]:  Discovery: 151 ERDs to publish
```

| Line | What it answers |
|------|-----------------|
| `Custom ERDs:` | How many ERDs the YAML declares, split by board. `primary` is the primary board; a physical board address is always `0x00`–`0xFE`. |
| `Subscription:` | How many ERDs the subscription bridge subscribed to, and the host board it subscribed from. |
| `Polled ERDs:` | Present only when it adds something: the per-board split of what the polling bridge actually polls (multi-board appliances), or, when nothing is polled, why — `no custom ERDs configured`, `custom ERDs all arrive by subscription`, or `N custom ERDs uncovered, no polling bridge`. The polled count itself is logged at INFO by the bridge as `Entered steady-state polling: N ERDs`. |
| `Feature bits:` | ERDs the feature API declared valid, and how many were skipped. `unsupported or unreadable` is deliberate: a read that timed out counts the same as a feature the appliance lacks, so the number is not a list of missing features. |
| `Discovery:` | One line per discovery run, not per boot — a normal boot whose stored discovery record is unchanged runs no discovery and prints nothing. |

The per-entry detail these lines replaced is still logged at VERBOSE, which compiles out below that level, so a device built with `logger: level: VERBOSE` still shows every ERD as it is cached or skipped.

### Monitor MQTT Traffic

```bash
# Subscribe to all bridge topics:
mosquitto_sub -h <broker> -u <user> -P <pass> -t 'geappliances/#' -v

# Subscribe to discovery topics:
mosquitto_sub -h <broker> -u <user> -P <pass> -t 'homeassistant/#' -v
```

### Check ESPHome API

The diagnostic sensors (ERD publish rate, cache entries, cache updates, MQTT publish rate) are available via the ESPHome API. Access them through the ESPHome dashboard or Home Assistant's ESPHome integration.