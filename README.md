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

# UART configuration for GEA3 communication
uart:
  id: gea3_uart
  tx_pin: GPIO21 #D6 on Xiao ESP32-C3
  rx_pin: GPIO20 #D7 on Xiao ESP32-C3
  baud_rate: 230400

# GE Appliances Bridge component
geappliances_bridge:
  # device_id: "YourDeviceId"  # Optional: Uncomment to use a custom device ID
  uart_id: gea3_uart
```

### Operation Modes

The component supports three modes for retrieving data from the appliance:

1. **Auto Mode (Default)** - The adapter starts with subscription mode and automatically falls back to polling mode if no ERD responses are received within 30 seconds. This provides the best of both worlds: real-time updates when possible, with automatic fallback for compatibility.

2. **Subscribe Mode** - The adapter subscribes to ERD updates from the appliance. The appliance pushes changes as they occur.

3. **Poll Mode** - The adapter actively polls the appliance for ERD values at a configurable interval.

#### Configuration Options

```yaml
geappliances_bridge:
  uart_id: gea3_uart
  mode: auto  # Default: auto. Options: auto, subscribe, poll
  polling_interval: 10000  # Default: 10000 ms (10 seconds), used when in polling mode
```

**Mode Recommendations:**
- **auto** (Default) - Recommended for most use cases. Provides automatic compatibility detection.
- **subscribe** - Use if you know your appliance supports subscription mode and want to ensure it stays in that mode.
- **poll** - Use if you know your appliance requires polling mode or you want explicit control over update frequency.

**Auto Mode Benefits:**
- Automatic compatibility detection
- No manual configuration required
- Falls back to polling if subscription doesn't work

**Subscribe Mode Benefits:**
- Lower network overhead
- Real-time updates when values change
- Best for appliances with reliable subscription support

**Poll Mode Benefits:**
- Explicit control over update frequency
- Guaranteed compatibility with all appliances
- Predictable network traffic

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

### ERD List Generation

The component uses an auto-generated ERD list (`erd_lists.h`) based on the [GE Appliances Public API Documentation](https://github.com/geappliances/public-appliance-api-documentation). The ERD list is automatically generated during the build process from `appliance_api_erd_definitions.json`.

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
