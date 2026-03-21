# Home Assistant Bridge for GE Appliances (ESPHome)

ESPHome external component for GE Appliances bridge supporting the GEA3 protocol.

Subscribes to data hosted by a GE Appliances product and publishes it to an MQTT server under `geappliances/<device ID>`. ERDs are identified by 16-bit identifiers and the raw binary data is published as a hex string to `geappliances/<device ID>/erd/<ERD ID>/value`. Data can be written to an ERD by writing a hex string of the appropriate size to `geappliances/<device ID>/erd/<ERD ID>/write`.

This is intended to be used with the MQTT server provided by Home Assistant, but it should work with other MQTT servers.

## Hardware

This component is designed for use with the **FirstBuild Home Assistant Adapter** featuring the SeeedStudio Xiao ESP32-C3 microcontroller. The adapter provides an RJ45 connection for GEA3 serial communication with GE Appliances.

Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/)

## Update Notifications

When a new release is published on this repository, **Home Assistant automatically shows an update notification** for the bridge.  The notification appears in the *Settings → Updates* panel and displays the currently-installed version alongside the latest available version.

The update check:
- Runs on every device start-up (after MQTT connects)
- Repeats every 24 hours while the device is running
- Queries the GitHub Releases API over HTTPS; no third-party service is involved

### How to apply an update

1. In your ESPHome YAML, change the `@vX.Y.Z` tag in the `external_components` block to the new version shown in the Home Assistant update notification.
2. Re-flash the device via ESPHome (OTA or USB).

```yaml
external_components:
  - source: github://joshualongenecker/home-assistant-bridge-esphome@v1.1.0  # ← bump this
    components: [ geappliances_bridge ]
```

## Configuration

Add to your ESPHome YAML configuration:

```yaml
esp32:
  board: seeed_xiao_esp32c3
  variant: esp32c3
  framework: 
    type: esp-idf

# Pin to a specific release tag so the update notification in Home Assistant
# correctly reflects the version that is installed on the device.
external_components:
  - source: github://joshualongenecker/home-assistant-bridge-esphome@v1.0.0
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

  - id: gea2_uart
    tx_pin: GPIO9  # D9 on Xiao ESP32-C3
    rx_pin: GPIO10  # D10 on Xiao ESP32-C3
    baud_rate: 19200
    rx_full_threshold: 1
    rx_timeout: 1

# GE Appliances Bridge component
geappliances_bridge:
  gea3_uart_id: gea3_uart
  # gea2_uart_id: gea2_uart
  # device_id: "YourDeviceId"     # Optional: Uncomment to use a custom device ID
  # mode: auto                    # Default: auto   Options: auto, subscribe, poll
  # polling_interval: 10000       # Default: 10000 ms (10 seconds), used when in polling mode
  # polling_onlypublish_onchange: false  # Default: false, only publish if value changed
```

## Configurable Parameters

### Mode

The `mode` parameter is **optional**. 

1. **Auto Mode (Default)** - The adapter starts with subscription mode and automatically falls back to polling mode if no ERD responses are received within 30 seconds. This provides the best of both worlds: real-time updates when possible, with automatic fallback for compatibility.

2. **Subscribe Mode** - The adapter subscribes to ERD updates from the appliance. The appliance pushes changes as they occur.

3. **Poll Mode** - The adapter actively polls the appliance for ERD values at a configurable interval `polling_interval`

### Autodiscovery

After connecting to the MQTT server, the component waits 5 seconds and then performs a protocol autodiscovery to find the appliance on the bus before generating a device ID.

- If `gea3_uart_id` is configured, a GEA3 broadcast is sent; if a board responds its address is used.
- Discovery repeats until at least one board is found.
- The first-responding board's address and protocol are used for all subsequent ERD communication.

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
- **Application-level integration tests** (simulated appliance testing)
- **Configuration-based testing** (different YAML scenarios)

#### Simulated Application Testing

The project includes comprehensive simulated application-level tests that validate complete workflows without physical hardware:

- **Configuration scenarios** - Testing different YAML configurations (subscription mode, polling modes with various intervals)
- **Multiple appliance types** - Dishwashers, refrigerators, washers with realistic ERD patterns
- **Device ID generation** - Simulating ERD reads for appliance identification
- **Subscription mode** - Testing ERD publications and MQTT publishing
- **Polling mode** - Testing periodic ERD polling with different intervals
- **MQTT write forwarding** - Testing write requests from Home Assistant
- **Error handling** - Testing failure scenarios and retry logic

See [test/simulation/README.md](test/simulation/README.md) for detailed information about the simulation testing framework and examples of how to create comprehensive appliance simulation tests.
