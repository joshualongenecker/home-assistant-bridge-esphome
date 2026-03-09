# BLE Commissioning Guide

Step-by-step instructions for provisioning a GE Appliances Bridge adapter using BLE — no serial cable required, no MQTT credentials in source code.

---

## Overview

The BLE commissioning flow consists of three stages:

1. **Flash** — build firmware with placeholder secrets and flash the adapter.
2. **Pair WiFi** — Home Assistant detects the adapter's BLE advertisement and provisions WiFi credentials automatically.
3. **Configure MQTT** — after the adapter is on WiFi, send MQTT credentials via the ESPHome native API once; they are persisted to flash and survive reboots.

---

## Prerequisites

- Home Assistant with the **ESPHome integration** installed.
- An MQTT broker (e.g. Mosquitto add-on in HA, or an external broker).
- A Bluetooth-capable Home Assistant host (or a Bluetooth proxy device).
- The [ESPHome CLI](https://esphome.io/guides/getting_started_command_line.html) or the ESPHome Dashboard to build and flash firmware.

---

## Stage 1 — Build and Flash the Firmware

### 1.1  Create a `secrets.yaml` with placeholder values

ESPHome requires that every `!secret` reference resolves at compile time, even when `ble_provisioning: true` is set. Use placeholder values — they are only used on the very first boot before real credentials are provisioned.

Create (or update) `secrets.yaml` next to your `example.yaml`:

```yaml
# secrets.yaml — placeholder values for initial BLE-provisioned build
# These are NOT your real credentials; they are overridden on first boot
# after you complete Stage 3 below.

api_encryption_key: "<generate a 32-byte base64 key — see note below>"
wifi_ssid: "unconfigured"          # overridden by BLE WiFi provisioning
wifi_password: "unconfigured"      # overridden by BLE WiFi provisioning
mqtt_broker: "unconfigured"        # overridden by configure_mqtt API call
mqtt_username: "unconfigured"      # overridden by configure_mqtt API call
mqtt_password: "unconfigured"      # overridden by configure_mqtt API call
```

> **Generate an API encryption key:**
> ```bash
> python3 -c "import base64, os; print(base64.b64encode(os.urandom(32)).decode())"
> ```
> Paste the output as the value of `api_encryption_key`.  Keep this key — you
> will need it when adding the device to the ESPHome integration in Home
> Assistant.

### 1.2  Ensure your YAML uses `esp32_improv` and `ble_provisioning: true`

Your `example.yaml` (or your own config) must contain:

```yaml
# BLE provisioning: broadcasts a BLE advertisement so HA can provision WiFi
esp32_improv:
  authorizer: none   # no physical button press required to authorize pairing

geappliances_bridge:
  id: geappliances_bridge_comp
  gea3_uart_id: gea3_uart
  ble_provisioning: true   # enables NVS credential storage

api:
  encryption:
    key: !secret api_encryption_key
  services:
    - service: configure_mqtt
      variables:
        broker: string
        port: int
        username: string
        password: string
      then:
        - lambda: |-
            id(geappliances_bridge_comp).configure_mqtt_credentials(broker, (uint16_t)port, username, password);
```

See [doc/example.yaml](example.yaml) for the complete, ready-to-use configuration.

### 1.3  Build and flash

```bash
# From the repo root (or wherever your yaml lives):
esphome run doc/example.yaml
```

Or, if using the ESPHome Dashboard, install the firmware from there.

After flashing, the adapter boots, finds no valid WiFi credentials, and starts broadcasting a BLE advertisement named after the device (e.g. `gea-esphome-XXXX`).

---

## Stage 2 — Pair WiFi via BLE in Home Assistant

### 2.1  Open the HA Notifications / Integrations page

1. In Home Assistant, go to **Settings → Devices & Services**.
2. In the **Discovered** section at the top you should see a new card:

   > **"New device discovered"** — `gea-esphome-XXXX` — *ESPHome*

   If it does not appear immediately, wait up to 60 seconds, then refresh the page.

> **Not seeing it?**
> - Verify Bluetooth is working on your HA host: **Settings → System → Hardware → Bluetooth**.
> - If your HA host has no Bluetooth, add a [Bluetooth proxy](https://esphome.io/components/bluetooth_proxy.html) on a nearby ESP32 device.
> - Confirm the adapter is powered and the BLE advertisement is active (the device log will show `esp32_improv: BLE advertising started`).

### 2.2  Provision WiFi

1. Click **Configure** on the discovered card.
2. Home Assistant opens the Improv BLE wizard and prompts for your WiFi SSID and password.
3. Enter your real WiFi credentials and click **Connect**.
4. Home Assistant sends the credentials over BLE; the adapter connects to WiFi, and the BLE advertisement stops.

> The adapter automatically reboots once after receiving the WiFi credentials. This is normal.

### 2.3  Home Assistant adds the device

After the adapter connects to WiFi, Home Assistant discovers it via the ESPHome native API and prompts you to add it as a device. Enter your `api_encryption_key` when prompted.

---

## Stage 3 — Configure MQTT Credentials

WiFi is now working, but the adapter does not yet know your MQTT broker address. Complete this stage once to push the real credentials.

### 3.1  Find the service name

In Home Assistant, go to **Developer Tools → Actions** (or *Services* in older versions).

Search for `configure_mqtt`. The service will be named:

```
esphome.<your_device_name>_configure_mqtt
```

For example: `esphome.gea_esphome_xxxx_configure_mqtt`

### 3.2  Call the service

Fill in the service call fields:

| Field      | Value                                      |
|------------|--------------------------------------------|
| `broker`   | Your MQTT broker IP or hostname (e.g. `192.168.1.100` or `homeassistant.local`) |
| `port`     | `1883` (standard) or `8883` (TLS)          |
| `username` | Your MQTT username                         |
| `password` | Your MQTT password                         |

In YAML format (paste into the *YAML mode* tab of the Actions UI):

```yaml
service: esphome.gea_esphome_xxxx_configure_mqtt
data:
  broker: "192.168.1.100"
  port: 1883
  username: "mqtt_user"
  password: "mqtt_password"
```

Click **Perform Action** (or *Call Service*).

### 3.3  Adapter reboots and connects

The adapter:
1. Writes the credentials to ESP32 NVS (non-volatile flash storage).
2. Reboots automatically.
3. On the next boot, reads the NVS credentials and uses them to connect to your MQTT broker — overriding the placeholder values from `secrets.yaml`.

You should see MQTT entities from `geappliances/<device-id>/erd/...` appear in Home Assistant within a few seconds.

### 3.4  Credentials persist across reboots

Once stored in NVS, the credentials survive power cycles and OTA updates. You only need to call `configure_mqtt` again if your broker address, port, username, or password changes.

---

## Quick-Reference Checklist

- [ ] Generate a valid `api_encryption_key` (32-byte base64).
- [ ] Put placeholder values in `secrets.yaml` for `mqtt_broker`, `mqtt_username`, `mqtt_password`, `wifi_ssid`, `wifi_password`.
- [ ] Set `ble_provisioning: true` and include `esp32_improv:` in your YAML.
- [ ] Build and flash the firmware (`esphome run example.yaml`).
- [ ] Go to **Settings → Devices & Services** in HA and click **Configure** on the discovered BLE device.
- [ ] Enter your real WiFi credentials in the Improv wizard.
- [ ] Add the device when HA prompts (enter your `api_encryption_key`).
- [ ] Go to **Developer Tools → Actions** and call `esphome.<name>_configure_mqtt` with your broker details.
- [ ] Confirm the adapter reboots and MQTT entities appear in HA.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| BLE device not discovered | No Bluetooth on HA host | Add a [Bluetooth proxy](https://esphome.io/components/bluetooth_proxy.html) |
| "configure_mqtt" service not found | Device not added to ESPHome integration | Complete Stage 2.3 first |
| MQTT entities don't appear after Stage 3 | Wrong broker IP or port | Check broker address; call `configure_mqtt` again with corrected values |
| Device keeps using old placeholder credentials | NVS not yet written | Stage 3 has not been completed yet |
| ESPHome compile fails with secrets error | Missing keys in `secrets.yaml` | Add all six placeholder keys listed in Stage 1.1 |
