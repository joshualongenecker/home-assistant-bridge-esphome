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
  name: gea-esphome-1
  friendly_name: gea-esphome

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
  tx_pin: GPIO21   # D6
  rx_pin: GPIO20   # D7
  baud_rate: 230400

# GE Appliances Bridge component
geappliances_bridge:
  # device_id: "YourDeviceId"  # Optional: Uncomment to use a custom device ID
  uart_id: gea3_uart
```

### Auto-Generated Device ID

The `device_id` parameter is **optional**. If not provided, the component will automatically generate a device ID by reading the following ERDs from the appliance:

- **Appliance Type** (ERD 0x0008) - Single byte enum (converted to string name, e.g., "Dishwasher")
- **Model Number** (ERD 0x0001) - 32 byte string
- **Serial Number** (ERD 0x0002) - 32 byte string

The auto-generated device ID format is: `ApplianceTypeName_ModelNumber_SerialNumber`

The appliance type names are loaded from the [GE Appliances Public API Documentation](https://github.com/geappliances/public-appliance-api-documentation) library during the ESPHome build process. The library is automatically downloaded and cached like other dependencies, giving you control over when to update the appliance type definitions.

Example:
```yaml
# Auto-generate device ID (recommended)
geappliances_bridge:
  uart_id: gea3_uart
  
# Or use a custom device ID
geappliances_bridge:
  device_id: "my_custom_id"
  uart_id: gea3_uart
```

Generated device ID example: `Dishwasher_ZL4200ABC_12345678` (for appliance type 6 - Dishwasher)

**Note:** The required libraries (`tiny`, `tiny-gea-api`, and `public-appliance-api-documentation`) are automatically fetched and compiled by ESPHome during the build process.

See [components/geappliances_bridge/example.yaml](components/geappliances_bridge/example.yaml) for the complete configuration example.

## Development

### Running Tests

This project includes unit tests for the core bridge functionality. The tests are built using CppUTest.

#### Prerequisites

Install CppUTest:

**Ubuntu/Debian:**
```bash
sudo apt-get install cpputest libcpputest-dev
```

**macOS:**
```bash
brew install cpputest
```

#### Running the Tests

Clone the repository with submodules:
```bash
git clone --recursive https://github.com/joshualongenecker/home-assistant-bridge-esphome.git
cd home-assistant-bridge-esphome
```

Build and run the tests:
```bash
make test
```

This will:
1. Compile the test suite
2. Run all unit tests
3. Display the test results

The tests cover:
- MQTT bridge functionality
- Subscription management
- ERD publication handling
- Uptime monitoring
