# home-assistant-bridge-esphome

ESPHome external component for GE Appliances bridge using the GEA3 protocol.

Subscribes to data hosted by a GE Appliances product using GEA3 and publishes it to an MQTT server under `geappliances/<device ID>`. ERDs are identified by 16-bit identifiers and the raw binary data is published as a hex string to `geappliances/<device ID>/erd/<ERD ID>/value`. Data can be written to an ERD by writing a hex string of the appropriate size to `geappliances/<device ID>/erd/<ERD ID>/write`.

This is intended to be used with the MQTT server provided by Home Assistant, but it should work with other MQTT servers.

## Hardware

This component is designed for use with the **FirstBuild Home Assistant Adapter** featuring the SeeedStudio Xiao ESP32-C3 microcontroller. The adapter provides an RJ45 connection for GEA3 serial communication with GE Appliances.

Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/)

## Configuration

Add to your ESPHome YAML configuration:

```yaml
substitutions:
  name: zoneline-esphome
  friendly_name: zoneline-esphome

packages:
  esphome.esp_web_tools_example: github://esphome/firmware/esp-web-tools/esp32c3.yaml@main

esphome:
  name: ${name}
  name_add_mac_suffix: false
  friendly_name: ${friendly_name}
  platformio_options:
   board_build.flash_mode: dio

esp32:
  board: seeed_xiao_esp32c3
  variant: esp32c3
  framework: 
    type: esp-idf

api:
  encryption:
    key: !secret api_encryption_key

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

# External component configuration
external_components:
  - source: github://joshualongenecker/home-assistant-bridge@develop
    components: [ geappliances_bridge ]

mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  port: 1883
  discovery: true
  discovery_prefix: homeassistant

# UART configuration for GEA3 communication
uart:
  id: gea3_uart
  tx_pin: GPIO21
  rx_pin: GPIO20
  baud_rate: 230400

# GE Appliances Bridge component
geappliances_bridge:
  device_id: "YourDeviceId"
  uart_id: gea3_uart
  client_address: 0xE4  # Default GEA3 client address
```

**Note:** The required C++ libraries (`tiny` and `tiny-gea-api`) are automatically fetched and compiled by ESPHome during the build process.

See [components/geappliances_bridge/example.yaml](components/geappliances_bridge/example.yaml) for the complete configuration example.
