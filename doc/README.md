# Documentation and Test Configurations

This directory contains ESPHome configuration examples and test files.

## Files

### example.yaml
Complete example configuration for the GE Appliances Bridge component. This shows how to configure the component in a real deployment with actual secrets.

### test-compile.yaml
Test configuration used by the CI/CD pipeline to verify that the component compiles correctly. This file is identical to example.yaml except it uses a local component source for testing.

### test-secrets.yaml
Template file containing dummy secret values for CI compilation testing. These values allow the configuration to compile without exposing real credentials.

**DO NOT use these values in production!**

## Usage

For local development or production, create your own `secrets.yaml` file with your actual credentials:

```yaml
wifi_ssid: "YourWiFiSSID"
wifi_password: "YourWiFiPassword"
mqtt_broker: "your.mqtt.broker"
mqtt_username: "your_username"
mqtt_password: "your_password"
api_encryption_key: "your_32_character_encryption_key"
```

The `secrets.yaml` file is excluded from version control via `.gitignore` to protect your credentials.

## CI/CD Testing

The GitHub Actions workflow automatically:
1. Copies `test-secrets.yaml` to `secrets.yaml` for compilation
2. Compiles `test-compile.yaml` to verify the component works
3. Cleans up the temporary secrets file

This ensures that every PR is tested for compilation errors before merging.
