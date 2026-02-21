# Hardware Configuration

## Overview

This component is designed for use with the **FirstBuild Home Assistant Adapter** featuring the SeeedStudio Xiao ESP32-C3 microcontroller and GEA3 serial interface.

## FirstBuild Home Assistant Adapter

The FirstBuild Home Assistant Adapter consists of:
- [Xiao ESP32C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) microcontroller
- Custom carrier board with RJ45 jack for GEA3 serial connection
- Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/)

Reference repository: [geappliances/home-assistant-adapter](https://github.com/geappliances/home-assistant-adapter)

## Pin Configuration

The FirstBuild adapter uses the following pins for GEA3 communication:

```yaml
uart:
  id: gea3_uart
  tx_pin: GPIO21 #D6
  rx_pin: GPIO20 #D7
  baud_rate: 230400
```

**Pin Mapping:**
- GPIO21 = TX (to appliance RX)
- GPIO20 = RX (from appliance TX)

## GEA3 Serial Connection

The GEA3 protocol requires:
- **Baud rate:** 230400 bps
- **Configuration:** 8 data bits, no parity, 1 stop bit (8N1)
- **Voltage level:** 3.3V TTL

The FirstBuild adapter carrier board handles the RJ45-to-serial conversion automatically.

## Physical Connection

Connect the FirstBuild adapter to your GE Appliance's GEA3 port using a standard Ethernet cable (RJ45).

## Troubleshooting

### No Communication with Appliance

1. **Check physical connection** - Ensure RJ45 cable is firmly seated
2. **Verify appliance compatibility** - Confirm appliance uses GEA3 protocol
3. **Check power** - Ensure both adapter and appliance are powered on
4. **Review logs** - Enable DEBUG logging to see UART activity

### Testing Configuration

Enable DEBUG logging to verify UART activity:

```yaml
logger:
  level: DEBUG
```

Look for:
- UART initialization messages
- Data received from appliance
- ERD value updates

## Additional Resources

- [ESPHome UART Component](https://esphome.io/components/uart.html)
- [Xiao ESP32C3 Documentation](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
- [GEA3 Protocol Information](https://github.com/geappliances/tiny-gea-api)
- [FirstBuild Home Assistant Adapter](https://firstbuild.com/inventions/home-assistant-adapter/)
