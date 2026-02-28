# home-assistant-bridge-esphome

ESPHome external component for GE Appliances bridge supporting both GEA3 and GEA2 protocols.

Subscribes to data hosted by a GE Appliances product and publishes it to an MQTT server under `geappliances/<device ID>`. ERDs are identified by 16-bit identifiers and the raw binary data is published as a hex string to `geappliances/<device ID>/erd/<ERD ID>/value`. Data can be written to an ERD by writing a hex string of the appropriate size to `geappliances/<device ID>/erd/<ERD ID>/write`.

This is intended to be used with the MQTT server provided by Home Assistant, but it should work with other MQTT servers.

## Hardware

This component is designed for use with the **FirstBuild Home Assistant Adapter** featuring the SeeedStudio Xiao ESP32-C3 microcontroller. The adapter provides an RJ45 connection for GEA3 serial communication with GE Appliances.

Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/)

## Configuration

Add to your ESPHome YAML configuration:

```yaml
esp32:
  board: seeed_xiao_esp32c3
  variant: esp32c3
  framework: 
    type: esp-idf

# External component configuration
external_components:
  - source: github://joshualongenecker/home-assistant-bridge@develop
    components: [ geappliances_bridge ]

# MQTT configuration for Home Assistant
mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  port: 1883
  discovery: true
  discovery_prefix: homeassistant

# UART configuration
uart:
  # GEA3 UART (newer appliances)
  - id: gea3_uart
    tx_pin: GPIO21  # D6 on Xiao ESP32-C3
    rx_pin: GPIO20  # D7 on Xiao ESP32-C3
    baud_rate: 230400

  # GEA2 UART (older appliances) - optional
  - id: gea2_uart
    tx_pin: GPIO9   # D9 on Xiao ESP32-C3
    rx_pin: GPIO10  # D10 on Xiao ESP32-C3
    baud_rate: 19200

# GE Appliances Bridge component
geappliances_bridge:
  gea3_uart_id: gea3_uart
  # gea2_uart_id: gea2_uart       # Optional: enable GEA2 support
  # device_id: "YourDeviceId"     # Optional: Uncomment to use a custom device ID
  # mode: auto                    # Default: auto   Options: auto, subscribe, poll
  # polling_interval: 10000       # Default: 10000 ms (10 seconds), used when in polling mode
  # gea_mode: auto                # Default: auto   Options: auto, gea3, gea2
  # gea3_address: 0xC0            # Default: 0xC0   Preferred GEA3 board address
  # gea2_address: 0xA0            # Default: 0xA0   Preferred GEA2 board address
```

## Configurable Parameters

### Mode

The `mode` parameter is **optional**. 

1. **Auto Mode (Default)** - The adapter starts with subscription mode and automatically falls back to polling mode if no ERD responses are received within 30 seconds. This provides the best of both worlds: real-time updates when possible, with automatic fallback for compatibility.

2. **Subscribe Mode** - The adapter subscribes to ERD updates from the appliance. The appliance pushes changes as they occur.

3. **Poll Mode** - The adapter actively polls the appliance for ERD values at a configurable interval `polling_interval`

### GEA Mode

The `gea_mode` parameter is **optional** and controls which protocol(s) are used during autodiscovery.

- **`auto` (Default)** - Tries GEA3 first, then GEA2.
- **`gea3`** - GEA3 only.
- **`gea2`** - GEA2 only.

### Board Address Preferences

`gea3_address` and `gea2_address` are **optional**. If the board at the preferred address responds during autodiscovery, it is used for device ID generation. If it does not respond, the first responder is used as a fallback.

- **`gea3_address`** - Default: `0xC0`
- **`gea2_address`** - Default: `0xA0`

### Autodiscovery

After connecting to the MQTT server, the component waits 20 seconds and then performs a protocol autodiscovery to find the appliance on the bus before generating a device ID:

1. Sends a GEA3 broadcast (→ `0xFF`) for ERD `0x0008` (Appliance Type); collects responses for 10 seconds.
2. Sends a GEA2 broadcast (→ `0xFF`) for ERD `0x0008` (Appliance Type); collects responses for 10 seconds.
3. If no boards respond, repeats steps 1–2 until at least one board is found.
4. Proceeds with device ID generation using the discovered board's address and protocol.

Each responding board is logged at DEBUG level with its address and appliance type.

### Auto-Generated Device ID

The `device_id` parameter is **optional**. If not provided, the component will automatically generate a device ID by reading the following ERDs from the appliance:

- **Appliance Type** (ERD 0x0008)
- **Model Number** (ERD 0x0001)
- **Serial Number** (ERD 0x0002)

The auto-generated device ID format is: `ApplianceTypeName_ModelNumber_SerialNumber`

The appliance type names are loaded from the [GE Appliances Public API Documentation](https://github.com/geappliances/public-appliance-api-documentation) library during the ESPHome build process

Generated device ID example: `Dishwasher_ZL4200ABC_12345678` (for appliance type 6 - Dishwasher)

See [doc/example.yaml](doc/example.yaml) for the complete configuration example.

## Development

### ERD List Generation

When polling, the device uses an auto-generated ERD list (`erd_lists.h`) based on the [GE Appliances Public API Documentation](https://github.com/geappliances/public-appliance-api-documentation). The ERD list is automatically generated during the build process from `appliance_api_erd_definitions.json`.

**To manually regenerate the ERD list:**
```bash
python3 scripts/generate_erd_lists.py
```

The generation script categorizes ERDs by appliance type based on their hex address ranges (common, refrigeration, laundry, dishwasher, water heater, range, air conditioning, water filter, small appliance, and energy ERDs). See [scripts/README.md](scripts/README.md) for more details.

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
