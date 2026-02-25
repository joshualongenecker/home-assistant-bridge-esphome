# Simulated Application-Level Testing

This directory contains application-level integration tests that simulate the complete behavior of the ESPHome GEA bridge without requiring physical hardware.

## Overview

The simulation testing framework allows testing of:
- Complete application workflows (device ID generation, subscription, polling)
- GEA3/GEA2 protocol interactions with mock appliance responses
- MQTT bridge behavior with simulated ERD reads/writes
- Mode switching (subscription mode fallback to polling mode)

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

### `application_level_test.cpp`

Contains high-level integration tests that validate complete workflows:

- **Device ID Generation** - Tests reading appliance type, model, and serial number ERDs
- **Subscription Mode** - Tests subscription request, ERD publications, and MQTT publishing
- **Polling Mode** - Tests periodic ERD polling and MQTT publishing
- **MQTT Write Forwarding** - Tests MQTT write requests being forwarded to the appliance
- **Mode Fallback** - Tests automatic fallback from subscription to polling mode

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

Here's an example of testing the complete subscription workflow:

```cpp
TEST(application_level, subscription_workflow)
{
  // Initialize bridge in subscription mode
  initialize_mqtt_bridge_subscription_mode();
  
  // Expect subscription request
  mock()
    .expectOneCall("subscribe")
    .withParameter("address", appliance_address)
    .andReturnValue(true);
  
  // Simulate subscription confirmed
  simulate_subscription_added(appliance_address);
  
  // Simulate ERD publication
  uint8_t data[] = {0x12, 0x34};
  mock()
    .expectOneCall("publish")
    .withParameter("erd", test_erd);
  
  simulate_erd_publication(test_erd, data, sizeof(data));
  
  mock().checkExpectations();
}
```

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
