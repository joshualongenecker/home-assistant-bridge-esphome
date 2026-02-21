# Hardware Configuration

## Overview
This component is designed to work with GE Appliances that use the GEA3 serial protocol. The hardware setup requires an ESP32 or ESP8266 board connected to the appliance's serial interface.

## Reference Hardware

The original implementation was designed for the **FirstBuild Home Assistant Adapter**, which consists of:
- [Xiao ESP32C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) microcontroller
- Custom carrier board with RJ45 jack for GEA3 serial connection
- Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/)

Reference repository: [geappliances/home-assistant-adapter](https://github.com/geappliances/home-assistant-adapter)

## Pin Configuration

### Xiao ESP32C3 (Recommended)

For the FirstBuild Home Assistant Adapter or any Xiao ESP32C3-based setup:

```yaml
uart:
  id: gea3_uart
  tx_pin: GPIO4  # D6 on Xiao ESP32C3
  rx_pin: GPIO5  # D7 on Xiao ESP32C3
  baud_rate: 230400
```

**Pin Mapping:**
- D6 (GPIO4) = TX
- D7 (GPIO5) = RX

### Generic ESP32

For other ESP32 boards, adjust the pins based on your hardware wiring. Common UART pin pairs:

```yaml
uart:
  id: gea3_uart
  tx_pin: GPIO17  # Example for UART2
  rx_pin: GPIO16  # Example for UART2
  baud_rate: 230400
```

### ESP8266

For ESP8266 boards:

```yaml
uart:
  id: gea3_uart
  tx_pin: GPIO15  # D8 on NodeMCU
  rx_pin: GPIO13  # D7 on NodeMCU
  baud_rate: 230400
```

**Note:** ESP8266 has limited hardware UART capabilities. Software serial may be required for some pin combinations.

## GEA3 Serial Connection

The GEA3 protocol requires:
- **Baud rate:** 230400 bps
- **Configuration:** 8 data bits, no parity, 1 stop bit (8N1)
- **Voltage level:** 3.3V TTL (most ESP boards use 3.3V logic)

### Physical Connection

Connect your ESP board to the appliance's GEA3 serial interface:

| ESP Pin | GEA3 Interface |
|---------|----------------|
| TX      | RX             |
| RX      | TX             |
| GND     | GND            |

**Important:** Ensure proper voltage levels. Do not connect 5V logic directly to 3.3V ESP boards.

## Wiring Diagram

For the FirstBuild adapter, the carrier board handles the RJ45-to-serial conversion. For custom implementations:

```
ESP32/ESP8266          GEA3 Appliance
    TX  ------------->  RX
    RX  <-------------  TX
    GND <-------------> GND
```

## Troubleshooting

### No Communication with Appliance

1. **Verify pin configuration** matches your hardware
2. **Check physical connections** (TX↔RX, RX↔TX, GND↔GND)
3. **Confirm baud rate** is set to 230400
4. **Test with original hardware** if available (FirstBuild adapter)

### Incorrect Pin Configuration Symptoms

- No data received from appliance
- MQTT topics not populated with ERD data
- GEA3 interface initialization errors in logs

### Testing Pin Configuration

Enable DEBUG logging to verify UART activity:

```yaml
logger:
  level: DEBUG
  logs:
    geappliances_bridge: VERBOSE
    uart: DEBUG
```

Look for:
- UART initialization messages
- Data received from appliance
- ERD value updates

## Board-Specific Notes

### Xiao ESP32C3
- Built-in USB-C for programming
- Low power consumption
- **D6 (GPIO4) and D7 (GPIO5)** are the correct pins for GEA3

### ESP32-DevKit
- Use UART2 (GPIO16/GPIO17) to keep UART0 for logging
- UART0 (GPIO1/GPIO3) used for USB serial

### ESP8266 NodeMCU
- Limited to UART0 or software serial
- May require disabling debug logging if using UART0 for GEA3

## Additional Resources

- [ESPHome UART Component](https://esphome.io/components/uart.html)
- [Xiao ESP32C3 Pinout](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
- [GEA3 Protocol Information](https://github.com/geappliances/tiny-gea-api)
- [FirstBuild Home Assistant Adapter](https://firstbuild.com/inventions/home-assistant-adapter/)
