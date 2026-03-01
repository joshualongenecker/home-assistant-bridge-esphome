# Simulated Application-Level Testing

This directory contains application-level integration tests that simulate the complete behavior of the ESPHome GEA bridge without requiring physical hardware.

## Overview

The simulation testing framework allows testing of:
- **Configuration-based testing** - Different YAML configuration scenarios
- Complete application workflows (device ID generation, subscription, polling)
- GEA3/GEA2 protocol interactions with mock appliance responses
- MQTT bridge behavior with simulated ERD reads/writes
- Mode switching (subscription mode fallback to polling mode)
- Multiple appliance types (dishwashers, refrigerators, washers)

## Architecture

The tests use the existing test double infrastructure:
- `tiny_gea3_erd_client_double` - Mocks the GEA3 ERD client
- `mqtt_client_double` - Mocks the MQTT client
- `tiny_timer_group_double` - Mocks the timer system

These doubles allow simulating:
1. **ERD Read Responses** - Simulate appliance responding to ERD read requests
2. **ERD Write Requests** - Capture and validate write requests from MQTT
3. **ERD Publications** - Simulate appliance publishing ERD updates (subscription mode)
4. **Subscription Management** - Simulate subscription lifecycle

## Test Structure

### `configuration_tests.cpp`

Comprehensive configuration-based tests that simulate different YAML scenarios:

- **Subscription Mode Tests** - Various appliance types (dishwasher, refrigerator, washer)
- **Polling Mode Tests** - Different polling intervals (5s, 10s, 30s)
- **ERD Handling** - Multiple ERD sizes and rapid updates
- **MQTT Write Requests** - Forwarding writes from Home Assistant
- **Mixed Scenarios** - Real-world appliance behavior patterns

Each test documents the YAML configuration it simulates.

### `application_level_test.cpp`

Contains high-level integration tests that validate complete workflows:

- **Device ID Generation** - Tests reading appliance type, model, and serial number ERDs
- **Subscription Mode** - Tests subscription request, ERD publications, and MQTT publishing
- **Polling Mode** - Tests periodic ERD polling and MQTT publishing
- **MQTT Write Forwarding** - Tests MQTT write requests being forwarded to the appliance
- **Mode Fallback** - Tests automatic fallback from subscription to polling mode

### `appliance_simulation_examples.cpp`

Example tests showing advanced simulation patterns and multi-step workflows.

## Running the Tests

The simulation tests are integrated into the main test suite:

```bash
make test
```

This will compile and run all tests, including the application-level simulation tests.

## Adding New Simulation Tests

To add new simulation tests:

1. Create a new test in `application_level_test.cpp` (or create a new test file)
2. Use the helper methods to simulate appliance behavior:
   - `simulate_erd_read_response()` - Simulate successful ERD read
   - `simulate_erd_read_failed()` - Simulate failed ERD read
   - `simulate_erd_publication()` - Simulate ERD publication from subscription
   - `simulate_subscription_added()` - Simulate successful subscription
   - `simulate_subscription_host_online()` - Simulate subscription host coming online

3. Use CppUTest expectations to verify behavior:
   ```cpp
   mock()
     .expectOneCall("read")
     .onObject(&erd_client)
     .withParameter("address", appliance_address)
     .withParameter("erd", target_erd)
     .andReturnValue(true);
   ```

## Example Test Scenario

Here's an example of testing a specific YAML configuration:

```yaml
# YAML Configuration being tested
geappliances_bridge:
  uart_id: gea3_uart
  mode: subscribe
```

```cpp
// Test implementation
TEST(configuration_based_tests, subscription_mode_dishwasher)
{
  // Initialize bridge in subscription mode
  configure_subscription_mode();
  simulate_subscription_added();
  
  // Simulate dishwasher cycle state change
  uint8_t cycle_state[] = {0x01};
  
  mock()
    .expectOneCall("register_erd")
    .withParameter("erd", ERD_CYCLE_STATE);
  
  mock()
    .expectOneCall("update_erd")
    .withParameter("erd", ERD_CYCLE_STATE)
    .withMemoryBufferParameter("value", cycle_state, sizeof(cycle_state));
  
  simulate_erd_publication(ERD_CYCLE_STATE, cycle_state, sizeof(cycle_state));
  
  mock().checkExpectations();
}
```

## Configuration Scenarios Tested

The test suite covers various YAML configuration scenarios:

1. **Subscription Mode with Different Appliances**
   ```yaml
   geappliances_bridge:
     uart_id: gea3_uart
     mode: subscribe
   ```
   - Dishwasher cycle operations
   - Refrigerator temperature monitoring
   - Washer/dryer status updates

2. **Polling Mode with Different Intervals**
   ```yaml
   geappliances_bridge:
     uart_id: gea3_uart
     mode: poll
     polling_interval: 10000  # or 5000, 30000
   ```
   - Fast polling (5 seconds) for responsive appliances
   - Default polling (10 seconds) for balanced performance
   - Slow polling (30 seconds) for reduced network traffic

3. **Auto Mode** (future implementation)
   ```yaml
   geappliances_bridge:
     uart_id: gea3_uart
     mode: auto
   ```
   - Start with subscription, fallback to polling if no activity

4. **Custom Device ID**
   ```yaml
   geappliances_bridge:
     uart_id: gea3_uart
     device_id: "MyCustomID"
   ```
   - Use configured ID instead of auto-generation

## Future Enhancements

Potential improvements to the simulation framework:

1. **Full GEA Protocol Simulator** - Implement a complete GEA3/GEA2 protocol simulator that:
   - Processes GEA packets at the byte level
   - Handles packet framing, CRC, and address fields
   - Simulates realistic timing and delays

2. **Appliance Type Templates** - Pre-configured appliance simulators for:
   - Dishwashers
   - Refrigerators
   - Ovens/Ranges
   - Laundry machines
   - Each with appropriate ERD lists and behaviors

3. **ESPHome Compilation Testing** - Generate and compile actual ESPHome configurations:
   - Test YAML configurations
   - Validate compilation without errors
   - Check generated C++ code

4. **Mock UART Layer** - Simulate UART communication at the byte level:
   - Inject transmission errors
   - Simulate baud rate mismatches
   - Test error recovery

5. **Performance Testing** - Measure and validate:
   - Response times
   - Memory usage
   - CPU utilization

## Benefits

The simulation testing framework provides:

1. **Early Validation** - Test application behavior without physical appliances
2. **Comprehensive Coverage** - Test edge cases and error conditions easily
3. **Fast Iteration** - Quick feedback during development
4. **Regression Prevention** - Catch issues before they reach hardware
5. **Documentation** - Tests serve as living documentation of expected behavior
