# home-assistant-bridge-esphome

ESPHome external component for GE Appliances bridge supporting both GEA3 and GEA2 protocols.

Subscribes to data hosted by a GE Appliances product using GEA3 (or polls using GEA2) and publishes it to an MQTT server under `geappliances/<device ID>`. ERDs are identified by 16-bit identifiers and the raw binary data is published as a hex string to `geappliances/<device ID>/erd/<ERD ID>/value`. Data can be written to an ERD by writing a hex string of the appropriate size to `geappliances/<device ID>/erd/<ERD ID>/write`.

This is intended to be used with the MQTT server provided by Home Assistant, but it should work with other MQTT servers.

## Protocols

### GEA3 (Newer Appliances)
- **Baud Rate:** 230400
- **Pins:** TX: GPIO21 (D6), RX: GPIO20 (D7) on Xiao ESP32-C3
- **Modes:** Subscribe (real-time) or Poll
- **Features:** Real-time updates when using subscribe mode

### GEA2 (Older Appliances)
- **Baud Rate:** 19200
- **Pins:** TX: D9, RX: D10 on Xiao ESP32-C3 (or any available GPIO)
- **Modes:** Poll only (slower)
- **Features:** Persistent ERD discovery using NVS storage

**Both protocols can run simultaneously** if you have appliances using different protocols.

## Hardware

This component is designed for use with the **FirstBuild Home Assistant Adapter** featuring the SeeedStudio Xiao ESP32-C3 microcontroller. The adapter provides an RJ45 connection for GEA3 serial communication with GE Appliances.

Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/)

## Configuration

### GEA3 Only Configuration

Add to your ESPHome YAML configuration:

```yaml
esp32:
  board: seeed_xiao_esp32c3
  variant: esp32c3
  framework: 
    type: esp-idf

# External component configuration
external_components:
  - source: github://joshualongenecker/home-assistant-bridge-esphome@develop
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
  uart_id: gea3_uart
  # device_id: "YourDeviceId"   # Default: auto generated    Uncomment to use a custom device ID
  # mode: auto                  # Default: auto              Options: auto, subscribe, poll
  # polling_interval: 10000     # Default: 10000 ms (10 seconds), used when in polling mode
```

### GEA2 Only Configuration

```yaml
# ... esp32, external_components, mqtt config same as above ...

# UART configuration for GEA2 communication (slower baudrate)
uart:
  id: gea2_uart
  tx_pin: GPIO8  # D9 on Xiao ESP32-C3 (or any available GPIO)
  rx_pin: GPIO9  # D10 on Xiao ESP32-C3 (or any available GPIO)
  baud_rate: 19200

# GE Appliances Bridge component with GEA2
geappliances_bridge:
  gea2_uart_id: gea2_uart
  # gea2_device_id: "MyGEA2Device"    # Optional: custom device ID, default: auto-generated
  # gea2_polling_interval: 3000       # Default: 3000 ms (3 seconds)
  # gea2_address: 0xE4                # Default: 0xE4 - board address on the GEA2 bus
```

### Both GEA3 and GEA2 Configuration

```yaml
# ... esp32, external_components, mqtt config same as above ...

# UART configuration for both protocols
uart:
  - id: gea3_uart
    tx_pin: GPIO21  # D6 on Xiao ESP32-C3
    rx_pin: GPIO20  # D7 on Xiao ESP32-C3
    baud_rate: 230400
  
  - id: gea2_uart
    tx_pin: GPIO8   # D9 on Xiao ESP32-C3
    rx_pin: GPIO9   # D10 on Xiao ESP32-C3
    baud_rate: 19200

# GE Appliances Bridge component supporting both protocols
geappliances_bridge:
  # GEA3 configuration
  uart_id: gea3_uart
  mode: auto
  polling_interval: 10000
  
  # GEA2 configuration (optional)
  gea2_uart_id: gea2_uart
  gea2_polling_interval: 3000
```

## Configurable Parameters

### GEA3 Parameters

#### Mode

The `mode` parameter is **optional**. 

1. **Auto Mode (Default)** - The adapter starts with subscription mode and automatically falls back to polling mode if no ERD responses are received within 30 seconds. This provides the best of both worlds: real-time updates when possible, with automatic fallback for compatibility.

2. **Subscribe Mode** - The adapter subscribes to ERD updates from the appliance. The appliance pushes changes as they occur.

3. **Poll Mode** - The adapter actively polls the appliance for ERD values at a configurable interval `polling_interval`

### Auto-Generated Device ID

The `device_id` parameter is **optional**. If not provided, the component will automatically generate a device ID by reading the following ERDs from the appliance:

- **Appliance Type** (ERD 0x0008)
- **Model Number** (ERD 0x0001)
- **Serial Number** (ERD 0x0002)

The auto-generated device ID format is: `ApplianceTypeName_ModelNumber_SerialNumber`

The appliance type names are loaded from the [GE Appliances Public API Documentation](https://github.com/geappliances/public-appliance-api-documentation) library during the ESPHome build process

Generated device ID example: `Dishwasher_ZL4200ABC_12345678` (for appliance type 6 - Dishwasher)

### GEA2 Parameters

#### gea2_uart_id

The `gea2_uart_id` parameter is **optional**. If provided, enables GEA2 protocol support for older appliances.

**Important:** GEA2 uses a different baud rate (19200) and requires different pins than GEA3.

#### gea2_device_id

The `gea2_device_id` parameter is **optional**. If not provided, the component will generate a device ID based on the GEA3 device ID (if available) with a `gea2_` prefix, or use `gea2_appliance` as a fallback.

#### gea2_polling_interval

The `gea2_polling_interval` parameter is **optional**. Default: 3000 ms (3 seconds).

GEA2 polling is slower than GEA3 due to the lower baud rate (19200 vs 230400). The component automatically discovers available ERDs by polling and stores them in non-volatile storage (NVS) to persist across reboots.

#### gea2_address

The `gea2_address` parameter is **optional**. Default: `0xE4`. This is the board's address on the GEA2 bus — the address the ESP32 uses as its source address when communicating with the appliance. Most users won't need to change this.

**ERD Discovery Process:**
1. Polls for appliance type (ERD 0x0008) to identify the appliance
2. Discovers available ERDs by polling common, energy, and appliance-specific ERDs
3. Stores discovered ERDs in NVS for fast recovery after reboot
4. Continuously polls discovered ERDs at the configured interval

See [doc/example.yaml](doc/example.yaml) for the complete configuration example.

## Development

### ERD List Generation

When polling, the device uses an auto-generated ERD list (`erd_lists.h`) based on the [GE Appliances Public API Documentation](https://github.com/geappliances/public-appliance-api-documentation). The ERD list is automatically generated during the build process from `appliance_api_erd_definitions.json`.

**To manually regenerate the ERD list:**
```bash
python3 scripts/generate_erd_lists.py
```

The generation script categorizes ERDs by appliance type based on their hex address ranges (common, refrigeration, laundry, dishwasher, water heater, range, air conditioning, water filter, small appliance, and energy ERDs). See [scripts/README.md](scripts/README.md) for more details.

### GEA2 Non-Volatile Storage

GEA2 uses ESP32's NVS (Non-Volatile Storage) to persist the discovered ERD list across reboots. This significantly speeds up initialization after power cycles, as the adapter doesn't need to rediscover all ERDs (which can take several minutes with GEA2's slow polling).

The stored data includes:
- List of discovered ERDs
- GEA address of the machine control board
- Count of ERDs in the polling list

If the appliance is disconnected for more than 60 seconds, the NVS is cleared and the discovery process restarts.

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
