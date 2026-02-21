# home-assistant-bridge
[![Tests](https://github.com/geappliances/home-assistant-bridge/actions/workflows/test.yml/badge.svg)](https://github.com/geappliances/home-assistant-bridge/actions/workflows/test.yml)

Subscribes to data hosted by a GE Appliances product using GEA3 and publishes it to an MQTT server under `geappliances/<device ID>`. ERDs are identified by 16-bit identifiers and the raw binary data is published as a hex string to `geappliances/<device ID>/erd/<ERD ID>/value`. Data can be written to an ERD by writing a hex string of the appropriate size to `geappliances/<device ID>/erd/<ERD ID>/write`.

This is intended to be used with the MQTT server provided by Home Assistant, but it should work with other MQTT servers.

## Usage

This library can be used in two ways:

### 1. Arduino/PlatformIO Library

Use as a traditional Arduino library with PlatformIO. See the [examples](examples/) directory for usage examples.

### 2. ESPHome External Component

Use as an ESPHome external component for seamless integration with Home Assistant.

#### ESPHome Configuration

Add to your ESPHome YAML configuration:

```yaml
# UART configuration for GEA3 communication
uart:
  id: gea3_uart
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 230400

# MQTT configuration
mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password

# External component
external_components:
  - source: github://joshualongenecker/home-assistant-bridge-esphome
    components: [ geappliances_bridge ]

# GE Appliances Bridge
geappliances_bridge:
  device_id: "my-appliance"
  uart_id: gea3_uart
  client_address: 0xE4  # Optional, default 0xE4
```

**Note:** The required C++ libraries (`tiny` and `tiny-gea-api`) are automatically included by the component.

See [components/geappliances_bridge/example.yaml](components/geappliances_bridge/example.yaml) for a complete configuration example.

For detailed information about ESPHome integration, see [ESPHOME_INTEGRATION.md](ESPHOME_INTEGRATION.md).
