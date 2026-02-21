# Testing the ESPHome External Component

This document describes how to test the GE Appliances Bridge as an ESPHome external component.

## Prerequisites

1. **ESPHome installed**
   ```bash
   pip install esphome
   ```

2. **ESP32 or ESP8266 development board** with:
   - Serial connection to GEA3 appliance
   - WiFi connectivity
   - Access to MQTT broker

3. **MQTT Broker** running and accessible (e.g., Mosquitto)

## Test Configuration

Create a test YAML file (e.g., `test-bridge.yaml`):

```yaml
esphome:
  name: test-ge-bridge
  platform: ESP32
  board: esp32dev

logger:
  level: DEBUG

wifi:
  ssid: "YourSSID"
  password: "YourPassword"

mqtt:
  broker: 192.168.1.100
  username: mqtt_user
  password: mqtt_pass

uart:
  id: gea3_uart
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 230400

external_components:
  # For local testing
  - source:
      type: local
      path: /path/to/home-assistant-bridge
    components: [ geappliances_bridge ]
  
  # For testing from GitHub (once pushed)
  # - source: github://joshualongenecker/home-assistant-bridge@branch-name
  #   components: [ geappliances_bridge ]

geappliances_bridge:
  device_id: "test-appliance"
  uart_id: gea3_uart
  client_address: 0xE4
```

## Compilation Test

Test that the component compiles without errors:

```bash
esphome compile test-bridge.yaml
```

Expected output should show:
- Successful compilation
- No missing header errors
- No linker errors

## Upload and Runtime Test

### 1. Flash the Device

```bash
esphome upload test-bridge.yaml
```

Or for initial flash via USB:
```bash
esphome run test-bridge.yaml
```

### 2. Monitor Logs

Watch the serial output to verify initialization:

```bash
esphome logs test-bridge.yaml
```

Expected log messages:
```
[C][geappliances_bridge:XXX]: GE Appliances Bridge:
[C][geappliances_bridge:XXX]:   Device ID: test-appliance
[C][geappliances_bridge:XXX]:   Client Address: 0xE4
[C][geappliances_bridge:XXX]:   UART Baud Rate: 230400
```

### 3. Verify MQTT Messages

Subscribe to all topics for your device:

```bash
mosquitto_sub -h 192.168.1.100 -u mqtt_user -P mqtt_pass -t '/geappliances/test-appliance/#' -v
```

You should see:
- `/geappliances/test-appliance/uptime` - Periodic uptime updates
- `/geappliances/test-appliance/erd/0xXXXX/value` - ERD values as hex strings (when appliance publishes)

### 4. Test Write Functionality

Publish a write request to an ERD:

```bash
mosquitto_pub -h 192.168.1.100 -u mqtt_user -P mqtt_pass \
  -t '/geappliances/test-appliance/erd/0x1234/write' \
  -m '0102030405'
```

Check logs for write request processing and result publication to:
- `/geappliances/test-appliance/erd/0x1234/write_result`

## Common Issues and Solutions

### Issue: Missing header files

**Error**: `fatal error: tiny_timer.h: No such file or directory`

**Solution**: Ensure git submodules are initialized:
```bash
cd /path/to/home-assistant-bridge
git submodule update --init --recursive
```

### Issue: MQTT not working

**Error**: No MQTT messages published

**Solutions**:
- Verify MQTT broker is reachable from ESP device
- Check MQTT credentials in YAML config
- Verify ESPHome MQTT component is initialized before geappliances_bridge
- Check logs for connection errors

### Issue: No UART data

**Error**: No ERD messages received

**Solutions**:
- Verify correct UART pins (TX/RX may need to be swapped)
- Confirm baud rate is 230400
- Check physical connection to appliance
- Verify appliance is powered and using GEA3 protocol
- Use logic analyzer to confirm data on UART lines

### Issue: Build failures on specific platforms

**Error**: Platform-specific compilation errors

**Solutions**:
- Verify ESP32/ESP8266 platform is correctly specified
- Check ESPHome version compatibility
- Try different board types in configuration

## Validation Checklist

- [ ] Component compiles without errors
- [ ] Device flashes successfully
- [ ] ESPHome logs show component initialization
- [ ] MQTT connection established
- [ ] Uptime messages published to MQTT
- [ ] ERD subscription successful (check logs)
- [ ] ERD values published when appliance sends updates
- [ ] Write requests can be sent via MQTT
- [ ] Write results are published
- [ ] Device stable during extended operation

## Advanced Testing

### Memory Usage

Monitor free heap to ensure no memory leaks:

Add to YAML:
```yaml
debug:
  update_interval: 5s

sensor:
  - platform: debug
    free:
      name: "Heap Free"
    loop_time:
      name: "Loop Time"
```

### Performance Testing

Test under load:
1. Send multiple write requests rapidly
2. Monitor ERD publications during high activity
3. Check for message loss or delays

### Integration Testing

Test with actual Home Assistant:
1. Configure Home Assistant MQTT integration
2. Subscribe to `/geappliances/+/#` in HA
3. Create MQTT sensors/switches for ERDs
4. Verify two-way communication

## Comparison with Arduino Version

To verify compatibility with the Arduino library version:

1. Flash device with Arduino example
2. Record MQTT topic structure and message format
3. Flash device with ESPHome version
4. Verify topics and message formats match exactly

Both should produce identical MQTT message structure:
```
/geappliances/<device_id>/uptime -> "12345"
/geappliances/<device_id>/erd/<erd_id>/value -> "AABBCCDD..."
/geappliances/<device_id>/erd/<erd_id>/write_result -> "success" or "failure (reason: X)"
```

## Reporting Issues

When reporting issues, please include:
1. Full ESPHome YAML configuration
2. ESPHome version (`esphome version`)
3. Platform (ESP32/ESP8266) and board type
4. Complete compilation output or logs
5. MQTT broker type and version
6. Appliance model (if relevant)
