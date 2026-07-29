# Hardware Configuration

## Overview

This component is designed for use with the **FirstBuild Home Assistant Adapter**

## FirstBuild Home Assistant Adapter

The FirstBuild Home Assistant Adapter consists of:
- [Xiao ESP32C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) microcontroller
- Custom carrier board with RJ45 jack for GEA3 or GEA2 serial connection
- Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/)

Reference repository: [geappliances/home-assistant-adapter](https://github.com/geappliances/home-assistant-adapter)


## Target Platform

This component can work with many of the Xiao ESP32 form factor microcontrollers if you want to try a different variant. They must be able to run the **ESP-IDF** platform.

### ESP32-C3 

The [Seeedstudio Xiao ESP32-C3](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html) is the microcontroller included with the Firstbuild Adapter:

```yaml
esp32:
  board: seeed_xiao_esp32c3
  framework: 
    type: esp-idf

# UART configuration
uart:
  # GEA3 UART (newer appliances)
  - id: gea3_uart
    tx_pin: GPIO21  # D6 on Xiao ESP32-C3
    rx_pin: GPIO20  # D7 on Xiao ESP32-C3
    baud_rate: 230400

  # GEA2 UART (newer appliances)
  - id: gea2_uart
    tx_pin: GPIO9   # D9 on Xiao ESP32-C3
    rx_pin: GPIO10  # D10 on Xiao ESP32-C3
    baud_rate: 19200
    rx_full_threshold: 1
    rx_timeout: 1
```

> **Why these settings matter:** GEA2's inter-byte timeout is 6 ms.  At
> 19200 baud the ESP32 default `rx_full_threshold` is ~19 bytes (≈10 ms),
> so hardware buffers bytes in batches with ~10 ms gaps between deliveries.
> That 10 ms gap exceeds the 6 ms timeout, causing the receive FSM to abort
> large-ERD responses mid-packet (no ACK is ever sent).  With
> `rx_full_threshold: 1` every byte is delivered to software within ~0.1 ms
> of arrival, keeping the inter-byte gap well below the 6 ms limit.

### ESP32-C6

The [Seeedstudio Xiao ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html) has been validated to work with this component as well:

```yaml
esp32:
  board: seeed_xiao_esp32c6
  framework: 
    type: esp-idf

# UART configuration
uart:
  # GEA3 UART (newer appliances)
  - id: gea3_uart
    tx_pin: GPIO16  # D6 on Xiao ESP32-C6
    rx_pin: GPIO17  # D7 on Xiao ESP32-C6
    baud_rate: 230400

  # GEA2 UART (newer appliances)
  - id: gea2_uart
    tx_pin: GPIO20  # D9 on Xiao ESP32-C6
    rx_pin: GPIO18  # D10 on Xiao ESP32-C6
    baud_rate: 19200
    rx_full_threshold: 1
    rx_timeout: 1
```

### Design Constraints

All architectural decisions in this component are driven by the single-core, no-PSRAM constraints of the ESP32-C3:

- **No dual-core concurrency on C3/C6.** The ESP32-C3 and ESP32-C6 each have a single CPU core. FreeRTOS tasks share the core through preemptive context switches — they never run in parallel. On dual-core ESP32-S3 the MQTT publisher task is pinned to Core 1 (same core as ESPHome's main loop) to prevent parallel access to shared state without locking. Synchronization primitives (`state_mutex`, `done_semaphore`) protect against torn reads/writes during context switches on all platforms.
- **No PSRAM.** All data structures, buffers, and task stacks must fit in the 320 KB of on-chip SRAM shared with the OS and ESPHome framework. This is why the MQTT publisher task uses a statically allocated 2 KB stack (`xTaskCreateStaticPinnedToCore` on dual-core, `xTaskCreateStatic` on single-core) and why ERD data is hex-encoded into fixed-size buffers.
- **DRAM budget:** ~275 KB of 320 KB total. ESPHome's core services (WiFi, MQTT, HTTP) consume ~100 KB of DRAM. The bridge component targets ~175 KB maximum, leaving headroom for the OS and future features.
- **Flash budget:** ~1.5 MB of 1.8 MB app partition. Any new embedded data must be balanced against this ceiling.


## Physical Connection

Connect the FirstBuild adapter to your GE Appliance's GEA3 port using a standard Ethernet cable (RJ45).

## Troubleshooting

### No Communication with Appliance

1. **Check physical connection** - Ensure RJ45 cable is firmly seated
2. **Verify appliance compatibility** - Confirm appliance uses GEA3 protocol
3. **Check power** - Verify that the lights on the ESP32 board are lit
4. **Review logs** - Enable DEBUG logging to see UART activity and autodiscovery results

If the logs show that no GEA2 or GEA3 boards can be identified, try another ethernet cable.

## Additional Resources

- [ESPHome UART Component](https://esphome.io/components/uart.html)
- [Xiao ESP32C3 Documentation](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
- [GEA3 Protocol Information](https://github.com/geappliances/tiny-gea-api)
- [FirstBuild Home Assistant Adapter](https://firstbuild.com/inventions/home-assistant-adapter/)
