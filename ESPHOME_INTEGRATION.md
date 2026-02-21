# ESPHome External Component Integration Guide

## Overview
This document outlines the steps required to convert the home-assistant-bridge library into an ESPHome external component, based on the ESPHome developer documentation.

## Steps to Create an ESPHome External Component

### 1. **Create Component Directory Structure**
ESPHome external components follow a specific directory structure:
```
components/
└── geappliances_bridge/
    ├── __init__.py           # Python configuration interface
    ├── geappliances_bridge.cpp  # C++ component implementation
    ├── geappliances_bridge.h    # C++ component header
    └── manifest.json         # Component metadata (optional)
```

### 2. **Create Python Configuration Interface (`__init__.py`)**
The `__init__.py` file provides the YAML configuration schema and registers the component with ESPHome:

**Key elements:**
- Import ESPHome configuration validation modules
- Define configuration schema with `cv.Schema()`
- Register the component with `CODEOWNERS`, `DEPENDENCIES`, etc.
- Define `to_code()` function to generate C++ code

**Required configuration parameters:**
- `device_id`: Unique identifier for the appliance
- `uart_id`: Reference to UART bus for GEA3 communication
- `mqtt_id`: Reference to MQTT client (ESPHome's native MQTT)
- `client_address`: Optional GEA3 client address (default 0xE4)

### 3. **Create C++ Component Wrapper**
Create a C++ class that inherits from `esphome::Component` and integrates with ESPHome's lifecycle:

**Key methods to implement:**
- `setup()`: Initialize the bridge (replaces Arduino `begin()`)
- `loop()`: Called on each iteration (existing functionality)
- `dump_config()`: Log configuration at startup

**Integration points:**
- Use ESPHome's UART component instead of Arduino Serial
- Use ESPHome's MQTT client instead of PubSubClient
- Use ESPHome's logging system (`ESP_LOG*`)

### 4. **Create Component Manifest**
Optional `manifest.json` file for metadata:
```json
{
  "name": "GE Appliances Bridge",
  "codeowners": ["@joshualongenecker"],
  "dependencies": [],
  "requirements": []
}
```

### 5. **Create Example Configuration**
Provide a complete ESPHome YAML configuration example:
```yaml
esphome:
  name: ge-appliance-bridge

external_components:
  - source: github://joshualongenecker/home-assistant-bridge-esphome
    components: [ geappliances_bridge ]

uart:
  id: gea3_uart
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 230400

mqtt:
  broker: 192.168.1.100
  username: !secret mqtt_username
  password: !secret mqtt_password

geappliances_bridge:
  device_id: "my-dishwasher"
  uart_id: gea3_uart
  client_address: 0xE4
```

**Note:** The C++ library dependencies are automatically added by the component.

### 6. **Update Repository Structure**
Organize the repository to support both Arduino library and ESPHome component:
```
home-assistant-bridge/
├── components/              # NEW: ESPHome external component
│   └── geappliances_bridge/
├── examples/               # Arduino examples
├── include/                # Shared C++ headers
├── src/                    # Shared C++ implementation
└── library.json            # PlatformIO library metadata
```

### 7. **Adapt MQTT Client Interface**
ESPHome has its own MQTT client with a different API. Options:
- **Option A**: Create an adapter layer that implements `i_mqtt_client.h` using ESPHome's MQTT
- **Option B**: Directly integrate with ESPHome's MQTT publish/subscribe methods

### 8. **Adapt UART Interface**
ESPHome's UART component differs from Arduino Serial:
- Use `esphome::uart::UARTComponent` instead of Arduino `Stream`
- Adapt the `tiny_uart_adapter` to work with ESPHome's UART

### 9. **Handle Dependencies**
The current library depends on:
- `arduino-tiny`: Provides tiny framework
- `tiny`: Core tiny library
- `tiny-gea-api`: GEA3 protocol implementation

These need to be:
- Included in the ESPHome component directory, OR
- Referenced as external dependencies in ESPHome build

### 10. **Testing Strategy**
1. Create a test ESPHome configuration
2. Compile the configuration with `esphome compile`
3. Flash to an ESP32/ESP8266 device
4. Verify UART communication with appliance
5. Verify MQTT messages are published correctly
6. Verify write operations work

## Key Differences: Arduino vs ESPHome

| Aspect | Arduino Library | ESPHome Component |
|--------|----------------|-------------------|
| Language | C++ only | Python (config) + C++ (runtime) |
| Configuration | Hardcoded in sketch | YAML configuration file |
| MQTT Client | PubSubClient | ESPHome native MQTT |
| UART | Arduino Serial | ESPHome UART component |
| Lifecycle | setup()/loop() | setup()/loop() + dump_config() |
| Logging | Serial.println() | ESP_LOG* macros |
| Dependencies | PlatformIO | ESPHome build system |

## Benefits of ESPHome Integration

1. **Declarative Configuration**: Users configure via YAML instead of C++ code
2. **Home Assistant Integration**: Automatic discovery and integration
3. **OTA Updates**: Built-in over-the-air firmware updates
4. **Web Interface**: Built-in web server for diagnostics
5. **Native ESPHome Features**: Sensors, switches, logging, etc.
6. **No Arduino IDE Required**: Users don't need to compile C++ code

## References
- ESPHome Developer Documentation: https://developers.esphome.io/
- ESPHome External Components: https://esphome.io/components/external_components.html
- ESPHome Custom Components: https://esphome.io/custom/custom_component.html
