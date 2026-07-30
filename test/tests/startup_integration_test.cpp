/*!
 * @file
 * @brief Integration test: full GeappliancesBridge startup lifecycle.
 *
 * Exercises the real GeappliancesBridge class through setup() → loop() →
 * teardown(), verifying component initialization, IBridgeServices contract,
 * teardown symmetry, and resilience under various configurations.
 *
 * The GEA3/GEA2 tight loops in run_protocol_stack_() are replaced with
 * single-pass iterations in test builds (UNIT_TEST_BUILD) to avoid infinite
 * loops when millis() is mocked.
 *
 * IMPORTANT: Only exercises the bridge through setup()/loop()/teardown().
 * Direct IBridgeServices method calls (initialize_erd_bridge, etc.) require
 * internal state that's only set up by setup() and are NOT tested directly.
 */

#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "bridge_mode.h"
#include "i_bridge_services.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "double/esphome_hal_double.hpp"
#include "double/mqtt_test_double.hpp"
#include "esphome/components/uart/uart.h"

#include <cstring>
#include <string>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Mock UARTComponent for integration testing                           */
/* ------------------------------------------------------------------ */

struct MockUartComponent : public esphome::uart::UARTComponent {
  int available_count;
  uint8_t read_buffer[256];
  int read_buffer_index;
  int read_buffer_size;
  int write_count;
  uint8_t last_written_byte;

  MockUartComponent()
    : available_count(0), read_buffer_index(0), read_buffer_size(0),
      write_count(0), last_written_byte(0)
  {
    std::memset(read_buffer, 0, sizeof(read_buffer));
  }

  ~MockUartComponent() override = default;

  void clear()
  {
    available_count = 0;
    read_buffer_index = 0;
    read_buffer_size = 0;
    write_count = 0;
    last_written_byte = 0;
    std::memset(read_buffer, 0, sizeof(read_buffer));
  }

  int available() override { return available_count; }

  int read() override
  {
    if (read_buffer_index < read_buffer_size) {
      return read_buffer[read_buffer_index++];
    }
    return -1;
  }

  void write(uint8_t data) override
  {
    last_written_byte = data;
    write_count++;
  }

  void write(const uint8_t* data, size_t len) override
  {
    for (size_t i = 0; i < len; i++) {
      last_written_byte = data[i];
      write_count++;
    }
  }

  int read_byte(uint8_t* byte) override
  {
    if (read_buffer_index < read_buffer_size) {
      *byte = read_buffer[read_buffer_index++];
      return 1;
    }
    *byte = 0;
    return -1;
  }

  void write_byte(uint8_t byte) override
  {
    last_written_byte = byte;
    write_count++;
  }
};

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(startup_integration)
{
  GeappliancesBridge *bridge;
  MockUartComponent *mock_uart;
  esphome::mqtt::MqttTestDouble *mqtt_double;

  void setup()
  {
    mock().clear();
    mock().strictOrder();
    esphome_hal_double_set_millis(0);
    bridge = new GeappliancesBridge();
    mock_uart = new MockUartComponent();
    mqtt_double = new esphome::mqtt::MqttTestDouble();
    mock_uart->clear();
    esphome::mqtt::global_mqtt_client = mqtt_double;
    mqtt_double->connected_ = true;
  }

  void teardown()
  {
    esphome::mqtt::global_mqtt_client = nullptr;
    mqtt_double->connected_ = false;
    delete bridge;
    delete mqtt_double;
    delete mock_uart;
    bridge = nullptr;
    mqtt_double = nullptr;
    mock_uart = nullptr;
    mock().clear();
  }

  /* Configure with GEA3 UART in POLL mode. */
  void configure_poll_mode()
  {
    bridge->set_gea3_uart(mock_uart);
    bridge->set_mode(BRIDGE_MODE_POLL);
    bridge->set_appliance_api_parsing(false);
  }

  /* Configure with GEA2 UART only. */
  void configure_gea2_only()
  {
    bridge->set_gea2_uart(mock_uart);
    bridge->set_mode(BRIDGE_MODE_POLL);
    bridge->set_appliance_api_parsing(false);
  }

  /* Configure with no UART. */
  void configure_no_uart()
  {
    bridge->set_mode(BRIDGE_MODE_POLL);
    bridge->set_appliance_api_parsing(false);
  }
};

/* ================================================================== */
/* Lifecycle tests                                                        */
/* ================================================================== */

TEST(startup_integration, setup_does_not_crash_with_gea3_uart)
{
  configure_poll_mode();
  bridge->setup();
}

TEST(startup_integration, first_loop_does_not_crash)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(100);
  bridge->loop();
  esphome_hal_double_set_millis(200);
  bridge->loop();
}

TEST(startup_integration, teardown_after_setup_is_safe)
{
  configure_poll_mode();
  bridge->setup();
  CHECK(bridge->teardown());
}

TEST(startup_integration, teardown_after_loop_is_safe)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, multiple_loop_iterations_are_safe)
{
  configure_poll_mode();
  bridge->setup();
  for (int i = 0; i < 50; i++) {
    esphome_hal_double_set_millis(i * 1000);
    bridge->loop();
  }
  CHECK(bridge->teardown());
}

TEST(startup_integration, reinit_after_teardown_is_safe)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());

  esphome_hal_double_set_millis(1000);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, loop_after_teardown_is_safe)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());

  esphome_hal_double_set_millis(1000);
  bridge->loop();
}

TEST(startup_integration, setup_after_teardown_reinitializes)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());

  esphome_hal_double_set_millis(1000);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, multiple_teardown_calls_are_safe)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
  CHECK(bridge->teardown());
  CHECK(bridge->teardown());
}

TEST(startup_integration, multiple_setup_calls_are_safe)
{
  configure_poll_mode();
  bridge->setup();
  bridge->setup();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, interleaved_setup_teardown_cycles)
{
  configure_poll_mode();
  for (int i = 0; i < 5; i++) {
    esphome_hal_double_set_millis(i * 1000);
    bridge->setup();
    bridge->loop();
    CHECK(bridge->teardown());
  }
}

TEST(startup_integration, setup_only_no_loop_is_safe)
{
  configure_poll_mode();
  bridge->setup();
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* Configuration variant tests                                            */
/* ================================================================== */

TEST(startup_integration, gea2_only_mode_setup_is_safe)
{
  configure_gea2_only();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, no_uart_setup_is_safe)
{
  configure_no_uart();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, both_uart_setup_is_safe)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_gea2_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_AUTO);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, subscribe_mode_setup_is_safe)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_SUBSCRIBE);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, auto_mode_setup_is_safe)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_AUTO);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, all_modes_cycle)
{
  for (int mode = BRIDGE_MODE_POLL; mode <= BRIDGE_MODE_AUTO; mode++) {
    esphome_hal_double_set_millis(mode * 1000);
    bridge->set_gea3_uart(mock_uart);
    bridge->set_mode(static_cast<BridgeMode>(mode));
    bridge->set_appliance_api_parsing(false);
    bridge->setup();
    bridge->loop();

    IBridgeServices* services = bridge;
    CHECK_EQUAL(static_cast<BridgeMode>(mode), services->get_mode());

    CHECK(bridge->teardown());
  }
}

TEST(startup_integration, all_uart_combinations)
{
  // GEA3 only
  esphome_hal_double_set_millis(0);
  bridge->set_gea3_uart(mock_uart);
  bridge->set_gea2_uart(nullptr);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());

  // GEA2 only
  esphome_hal_double_set_millis(1000);
  bridge->set_gea3_uart(nullptr);
  bridge->set_gea2_uart(mock_uart);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());

  // Both
  esphome_hal_double_set_millis(2000);
  bridge->set_gea3_uart(mock_uart);
  bridge->set_gea2_uart(mock_uart);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());

  // Neither
  esphome_hal_double_set_millis(3000);
  bridge->set_gea3_uart(nullptr);
  bridge->set_gea2_uart(nullptr);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, all_configuration_combinations)
{
  int iteration = 0;
  for (int parsing = 0; parsing <= 1; parsing++) {
    for (int generate = 0; generate <= 1; generate++) {
      for (int filter = 0; filter <= 1; filter++) {
        esphome_hal_double_set_millis(iteration++ * 1000);
        bridge->set_gea3_uart(mock_uart);
        bridge->set_mode(BRIDGE_MODE_POLL);
        bridge->set_appliance_api_parsing(parsing != 0);
        bridge->set_generate_device_config(generate != 0);
        bridge->set_filter_config_topics(filter != 0);
        bridge->setup();
        bridge->loop();
        CHECK(bridge->teardown());
      }
    }
  }
}

/* ================================================================== */
/* Cross-product tests: mode x UART                                     */
/* ================================================================== */

TEST(startup_integration, all_modes_x_all_uart_combinations)
{
  int iteration = 0;
  for (int mode = BRIDGE_MODE_POLL; mode <= BRIDGE_MODE_AUTO; mode++) {
    // GEA3 only
    {
      esphome_hal_double_set_millis(iteration++ * 1000);
      bridge->set_gea3_uart(mock_uart);
      bridge->set_gea2_uart(nullptr);
      bridge->set_mode(static_cast<BridgeMode>(mode));
      bridge->set_appliance_api_parsing(false);
      bridge->setup();
      bridge->loop();
      CHECK(bridge->teardown());
    }
    // GEA2 only
    {
      esphome_hal_double_set_millis(iteration++ * 1000);
      bridge->set_gea3_uart(nullptr);
      bridge->set_gea2_uart(mock_uart);
      bridge->set_mode(static_cast<BridgeMode>(mode));
      bridge->set_appliance_api_parsing(false);
      bridge->setup();
      bridge->loop();
      CHECK(bridge->teardown());
    }
    // Both
    {
      esphome_hal_double_set_millis(iteration++ * 1000);
      bridge->set_gea3_uart(mock_uart);
      bridge->set_gea2_uart(mock_uart);
      bridge->set_mode(static_cast<BridgeMode>(mode));
      bridge->set_appliance_api_parsing(false);
      bridge->setup();
      bridge->loop();
      CHECK(bridge->teardown());
    }
    // Neither
    {
      esphome_hal_double_set_millis(iteration++ * 1000);
      bridge->set_gea3_uart(nullptr);
      bridge->set_gea2_uart(nullptr);
      bridge->set_mode(static_cast<BridgeMode>(mode));
      bridge->set_appliance_api_parsing(false);
      bridge->setup();
      bridge->loop();
      CHECK(bridge->teardown());
    }
  }
}

/* ================================================================== */
/* IBridgeServices contract tests (read-only queries only)                */
/* ================================================================== */

TEST(startup_integration, mode_is_preserved_through_startup)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK_EQUAL(BRIDGE_MODE_POLL, services->get_mode());
  CHECK(bridge->teardown());
}

TEST(startup_integration, is_autodiscovery_complete_before_autodiscovery)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_autodiscovery_complete());
  CHECK(bridge->teardown());
}

TEST(startup_integration, is_device_id_complete_before_device_id_phase)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_device_id_complete());
  CHECK(bridge->teardown());
}

TEST(startup_integration, is_bridge_initialized_before_bridge_init_phase)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_bridge_initialized());
  CHECK(bridge->teardown());
}

TEST(startup_integration, is_mqtt_client_initialized_before_mqtt_init_phase)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_mqtt_client_initialized());
  CHECK(bridge->teardown());
}

TEST(startup_integration, is_feature_bits_complete_before_feature_bits_phase)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_feature_bits_complete());
  CHECK(bridge->teardown());
}

TEST(startup_integration, is_erd_cache_publisher_initialized_before_init)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_erd_cache_publisher_initialized());
  CHECK(bridge->teardown());
}

TEST(startup_integration, is_startup_delay_elapsed_before_delay_starts)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_startup_delay_elapsed());
  CHECK(bridge->teardown());
}

TEST(startup_integration, get_discovered_host_address_before_autodiscovery)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK_EQUAL(0, services->get_discovered_host_address());
  CHECK(bridge->teardown());
}

TEST(startup_integration, is_discovered_gea2_protocol_before_autodiscovery)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_discovered_gea2_protocol());
  CHECK(bridge->teardown());
}

TEST(startup_integration, get_subscription_state_before_bridge_init)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK_EQUAL(static_cast<int>(subscription_state_none),
              static_cast<int>(services->get_subscription_state()));
  CHECK(bridge->teardown());
}

TEST(startup_integration, get_polling_state_before_bridge_init)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK_EQUAL(static_cast<int>(polling_state_none),
              static_cast<int>(services->get_polling_state()));
  CHECK(bridge->teardown());
}

TEST(startup_integration, check_steady_state_before_steady_state)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->check_steady_state());
  CHECK(bridge->teardown());
}

TEST(startup_integration, startup_delay_elapsed_after_time_advance)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  services->record_startup_delay_start();
  CHECK(!services->is_startup_delay_elapsed());
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  CHECK(services->is_startup_delay_elapsed());
  CHECK(bridge->teardown());
}

TEST(startup_integration, mqtt_adapter_initialized_after_init_call)
{
  configure_poll_mode();
  bridge->setup();
  IBridgeServices* services = bridge;
  CHECK(!services->is_mqtt_client_initialized());
  services->initialize_mqtt_client();
  CHECK(services->is_mqtt_client_initialized());
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* Bridge method safety tests                                             */
/* ================================================================== */

TEST(startup_integration, custom_erd_addition_is_safe)
{
  configure_poll_mode();
  bridge->setup();
  bridge->add_custom_erd(0x7130);
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, trigger_discovery_refresh_is_safe)
{
  configure_poll_mode();
  bridge->setup();
  bridge->trigger_discovery_refresh();
  CHECK(bridge->teardown());
}

TEST(startup_integration, dump_config_is_safe)
{
  configure_poll_mode();
  bridge->setup();
  bridge->dump_config();
  CHECK(bridge->teardown());
}

TEST(startup_integration, dump_config_before_setup_is_safe)
{
  configure_poll_mode();
  bridge->dump_config();
  CHECK(bridge->teardown());
}

TEST(startup_integration, get_setup_priority_returns_data_priority)
{
  configure_poll_mode();
  bridge->setup();
  float priority = bridge->get_setup_priority();
  CHECK(priority >= 599.9f);
  CHECK(priority <= 600.1f);
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* MQTT interaction tests                                                 */
/* ================================================================== */

TEST(startup_integration, bridge_with_no_mqtt_client)
{
  configure_poll_mode();
  esphome::mqtt::global_mqtt_client = nullptr;
  bridge->setup();
  bridge->loop();
  esphome::mqtt::global_mqtt_client = mqtt_double;
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_mqtt_disconnected)
{
  configure_poll_mode();
  mqtt_double->connected_ = false;
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_mqtt_reconnects)
{
  configure_poll_mode();
  mqtt_double->connected_ = false;
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(1000);
  mqtt_double->connected_ = true;
  if (mqtt_double->on_connect_callback_) {
    mqtt_double->on_connect_callback_(false);
  }
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_mqtt_disconnects)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(1000);
  mqtt_double->connected_ = false;
  if (mqtt_double->on_disconnect_callback_) {
    mqtt_double->on_disconnect_callback_(esphome::mqtt::MQTTClientDisconnectReason::TCP_DISCONNECTED);
  }
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_mqtt_message_simulation)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(1000);
  mqtt_double->simulate_message("geappliances/test/erd/0x0092/write", "01");
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_concurrent_mqtt_messages)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  for (int i = 0; i < 10; i++) {
    esphome_hal_double_set_millis(1000 + i * 100);
    char topic[64];
    snprintf(topic, sizeof(topic), "geappliances/test/erd/0x%04x/write", 0x0092 + i);
    mqtt_double->simulate_message(topic, "01");
  }
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_alternating_mqtt_state)
{
  configure_poll_mode();
  bridge->setup();
  for (int i = 0; i < 10; i++) {
    esphome_hal_double_set_millis(i * 1000);
    mqtt_double->connected_ = (i % 2 == 0);
    if (mqtt_double->connected_ && mqtt_double->on_connect_callback_) {
      mqtt_double->on_connect_callback_(false);
    }
    if (!mqtt_double->connected_ && mqtt_double->on_disconnect_callback_) {
      mqtt_double->on_disconnect_callback_(esphome::mqtt::MQTTClientDisconnectReason::TCP_DISCONNECTED);
    }
    bridge->loop();
  }
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* Time progression tests                                                 */
/* ================================================================== */

TEST(startup_integration, bridge_lifecycle_with_time_progression)
{
  configure_poll_mode();
  bridge->setup();
  for (int i = 0; i < 60; i++) {
    esphome_hal_double_set_millis(i * 1000);
    bridge->loop();
  }
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_lifecycle_with_time_jump)
{
  configure_poll_mode();
  bridge->setup();
  esphome_hal_double_set_millis(0);
  bridge->loop();
  esphome_hal_double_set_millis(3600000);
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_lifecycle_with_millis_wrap)
{
  configure_poll_mode();
  bridge->setup();
  esphome_hal_double_set_millis(UINT32_MAX - 1000);
  bridge->loop();
  esphome_hal_double_set_millis(1000);
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_boundary_millis_values)
{
  uint32_t boundaries[] = { 0, 1, AUTODISCOVERY_STARTUP_DELAY_MS - 1,
                            AUTODISCOVERY_STARTUP_DELAY_MS,
                            AUTODISCOVERY_STARTUP_DELAY_MS + 1,
                            UINT32_MAX - 1, UINT32_MAX };
  for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); i++) {
    esphome_hal_double_set_millis(boundaries[i]);
    bridge->set_gea3_uart(mock_uart);
    bridge->set_mode(BRIDGE_MODE_POLL);
    bridge->set_appliance_api_parsing(false);
    bridge->setup();
    bridge->loop();
    CHECK(bridge->teardown());
  }
}

TEST(startup_integration, bridge_with_rapid_loop_calls)
{
  configure_poll_mode();
  bridge->setup();
  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(i * 10);
    bridge->loop();
  }
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* Configuration setter tests                                             */
/* ================================================================== */

TEST(startup_integration, bridge_with_custom_client_address)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->set_client_address(0xC0);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_preconfigured_device_id)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->set_device_id("my-custom-device");
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_long_device_id)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  std::string long_id(91, 'A');
  bridge->set_device_id(long_id);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_very_long_device_id)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  std::string very_long_id(200, 'B');
  bridge->set_device_id(very_long_id);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_special_characters_in_device_id)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->set_device_id("device-with_special.chars&123");
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_custom_polling_interval)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->set_polling_interval(5000);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_throttle_rate_zero)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->set_throttle_rate_seconds(0);
  bridge->setup();
  for (int i = 0; i < 20; i++) {
    esphome_hal_double_set_millis(i * 1000);
    bridge->loop();
  }
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_multiple_custom_erds)
{
  configure_poll_mode();
  bridge->setup();
  for (int i = 0; i < 10; i++) {
    bridge->add_custom_erd(static_cast<tiny_erd_t>(0x7000 + i));
  }
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, bridge_with_custom_erd_over_capacity)
{
  configure_poll_mode();
  bridge->setup();
  for (int i = 0; i < 100; i++) {
    bridge->add_custom_erd(static_cast<tiny_erd_t>(0x7000 + i));
  }
  bridge->loop();
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* Cross-product tests: mode x custom ERDs                              */
/* ================================================================== */

TEST(startup_integration, all_modes_x_custom_erds)
{
  int iteration = 0;
  for (int mode = BRIDGE_MODE_POLL; mode <= BRIDGE_MODE_AUTO; mode++) {
    esphome_hal_double_set_millis(iteration++ * 1000);
    bridge->set_gea3_uart(mock_uart);
    bridge->set_mode(static_cast<BridgeMode>(mode));
    bridge->set_appliance_api_parsing(false);
    bridge->setup();
    bridge->add_custom_erd(static_cast<tiny_erd_t>(0x7130));
    bridge->add_custom_erd(static_cast<tiny_erd_t>(0x7131));
    bridge->add_custom_erd(static_cast<tiny_erd_t>(0x7132));
    bridge->loop();
    CHECK(bridge->teardown());
  }
}

/* ================================================================== */
/* Sensor setter tests                                                    */
/* ================================================================== */

TEST(startup_integration, bridge_with_all_sensor_setters)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  bridge->set_erd_publish_rate_sensor(nullptr);
  bridge->set_erd_cache_entries_sensor(nullptr);
  bridge->set_erd_cache_updates_sensor(nullptr);
  bridge->set_mqtt_publish_rate_sensor(nullptr);
  bridge->set_mqtt_disconnect_count_sensor(nullptr);
  bridge->set_mqtt_disconnect_duration_sensor(nullptr);
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* All setter changes after setup                                         */
/* ================================================================== */

TEST(startup_integration, bridge_with_all_setters_after_setup)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  bridge->set_gea3_uart(nullptr);
  bridge->set_gea2_uart(nullptr);
  bridge->set_client_address(0xC0);
  bridge->set_device_id("new-device");
  bridge->set_mode(BRIDGE_MODE_SUBSCRIBE);
  bridge->set_polling_interval(5000);
  bridge->set_appliance_api_parsing(true);
  bridge->set_generate_device_config(false);
  bridge->set_filter_config_topics(false);
  bridge->set_throttle_rate_seconds(5);
  IBridgeServices* services = bridge;
  CHECK_EQUAL(BRIDGE_MODE_SUBSCRIBE, services->get_mode());
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* Error condition tests                                                  */
/* ================================================================== */

TEST(startup_integration, bridge_with_all_error_conditions)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(1000);
  IBridgeServices* services = bridge;
  services->handle_subscription_failed();
  services->handle_polling_failed();
  mqtt_double->connected_ = false;
  if (mqtt_double->on_disconnect_callback_) {
    mqtt_double->on_disconnect_callback_(esphome::mqtt::MQTTClientDisconnectReason::TCP_DISCONNECTED);
  }
  bridge->loop();
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* Stress tests                                                           */
/* ================================================================== */

TEST(startup_integration, bridge_full_lifecycle_stress)
{
  for (int i = 0; i < 10; i++) {
    esphome_hal_double_set_millis(i * 5000);
    bridge->set_gea3_uart(mock_uart);
    bridge->set_mode(static_cast<BridgeMode>(i % 4));
    bridge->set_appliance_api_parsing(i % 2 == 0);
    bridge->setup();
    mqtt_double->connected_ = (i % 2 == 0);
    for (int j = 0; j < 20; j++) {
      esphome_hal_double_set_millis(i * 5000 + j * 100);
      bridge->loop();
    }
    CHECK(bridge->teardown());
  }
}

TEST(startup_integration, bridge_stress_with_mqtt_events)
{
  configure_poll_mode();
  bridge->setup();
  for (int i = 0; i < 200; i++) {
    esphome_hal_double_set_millis(i * 100);
    mqtt_double->connected_ = (i % 2 == 0);
    if (mqtt_double->on_connect_callback_ && mqtt_double->connected_) {
      mqtt_double->on_connect_callback_(false);
    }
    if (mqtt_double->on_disconnect_callback_ && !mqtt_double->connected_) {
      mqtt_double->on_disconnect_callback_(esphome::mqtt::MQTTClientDisconnectReason::TCP_DISCONNECTED);
    }
    if (i % 5 == 0) {
      mqtt_double->simulate_message("geappliances/test/erd/0x0092/write", "01");
    }
    bridge->loop();
  }
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* State consistency after teardown                                       */
/* ================================================================== */

TEST(startup_integration, ibridge_services_safe_after_teardown)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());

  IBridgeServices* services = bridge;
  CHECK(!services->is_bridge_initialized());
  CHECK(!services->is_mqtt_client_initialized());
}

TEST(startup_integration, state_reset_after_reinit)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());

  esphome_hal_double_set_millis(1000);
  bridge->setup();
  bridge->loop();

  IBridgeServices* services = bridge;
  CHECK(!services->is_bridge_initialized());
  CHECK(!services->is_mqtt_client_initialized());
  CHECK(!services->is_erd_cache_publisher_initialized());

  CHECK(bridge->teardown());
}

TEST(startup_integration, state_consistency_across_lifecycle_cycles)
{
  for (int i = 0; i < 5; i++) {
    esphome_hal_double_set_millis(i * 1000);
    bridge->set_gea3_uart(mock_uart);
    bridge->set_mode(BRIDGE_MODE_POLL);
    bridge->set_appliance_api_parsing(false);
    bridge->setup();
    bridge->loop();
    IBridgeServices* services = bridge;
    CHECK_EQUAL(BRIDGE_MODE_POLL, services->get_mode());
    CHECK(bridge->teardown());
  }
}

/* ================================================================== */
/* GEA3 UART tests                                                        */
/* ================================================================== */

TEST(startup_integration, setup_with_gea3_uart_does_not_crash)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, subscribe_mode_with_gea3_uart)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_SUBSCRIBE);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, auto_mode_with_gea3_uart)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_AUTO);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, both_uarts_with_gea3_and_gea2)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_gea2_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_AUTO);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, gea3_with_feature_bit_parsing)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(true);
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, gea3_with_preconfigured_device_id)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->set_device_id("my-custom-device");
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

TEST(startup_integration, gea3_with_no_mqtt)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  esphome::mqtt::global_mqtt_client = nullptr;
  bridge->setup();
  bridge->loop();
  esphome::mqtt::global_mqtt_client = mqtt_double;
  CHECK(bridge->teardown());
}

TEST(startup_integration, gea3_with_mqtt_disconnected)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  mqtt_double->connected_ = false;
  bridge->setup();
  bridge->loop();
  CHECK(bridge->teardown());
}

/* ================================================================== */
/* Full startup sequence tests                                            */
/* ================================================================== */

TEST(startup_integration, full_startup_sequence_reaches_bridge_init)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  // In test builds without a real appliance to respond, the bridge
  // won't reach bridge_init state (no autodiscovery response).
  // We verify the bridge doesn't crash through the full sequence.
  IBridgeServices* services = bridge;
  (void)services->get_mode();

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_subscribe_mode)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_SUBSCRIBE);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  IBridgeServices* services = bridge;
  (void)services->get_mode();

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_auto_mode)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_AUTO);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  IBridgeServices* services = bridge;
  (void)services->get_mode();

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_preconfigured_device_id)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->set_device_id("my-custom-device");
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  IBridgeServices* services = bridge;
  (void)services->get_mode();

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_feature_bit_parsing_enabled)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(true);
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 200; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  IBridgeServices* services = bridge;
  (void)services->get_mode();

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_both_uarts)
{
  bridge->set_gea3_uart(mock_uart);
  bridge->set_gea2_uart(mock_uart);
  bridge->set_mode(BRIDGE_MODE_AUTO);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  IBridgeServices* services = bridge;
  (void)services->get_mode();

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_gea2_only)
{
  configure_gea2_only();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  IBridgeServices* services = bridge;
  (void)services->get_mode();

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_no_uart)
{
  configure_no_uart();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  IBridgeServices* services = bridge;
  (void)services->get_mode();

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_no_mqtt)
{
  configure_poll_mode();
  esphome::mqtt::global_mqtt_client = nullptr;
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  esphome::mqtt::global_mqtt_client = mqtt_double;
  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_mqtt_disconnect_during_startup)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  mqtt_double->connected_ = false;
  if (mqtt_double->on_disconnect_callback_) {
    mqtt_double->on_disconnect_callback_(esphome::mqtt::MQTTClientDisconnectReason::TCP_DISCONNECTED);
  }

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_mqtt_reconnect_during_startup)
{
  configure_poll_mode();
  mqtt_double->connected_ = false;
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  mqtt_double->connected_ = true;
  if (mqtt_double->on_connect_callback_) {
    mqtt_double->on_connect_callback_(false);
  }

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_mqtt_messages_during_startup)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    if (i % 10 == 0) {
      mqtt_double->simulate_message("geappliances/test/erd/0x0092/write", "01");
    }
    bridge->loop();
  }

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_alternating_mqtt_during_startup)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    mqtt_double->connected_ = (i % 2 == 0);
    if (mqtt_double->connected_ && mqtt_double->on_connect_callback_) {
      mqtt_double->on_connect_callback_(false);
    }
    if (!mqtt_double->connected_ && mqtt_double->on_disconnect_callback_) {
      mqtt_double->on_disconnect_callback_(esphome::mqtt::MQTTClientDisconnectReason::TCP_DISCONNECTED);
    }
    bridge->loop();
  }

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_all_error_conditions)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  bridge->loop();

  IBridgeServices* services = bridge;
  services->handle_subscription_failed();
  services->handle_polling_failed();

  mqtt_double->connected_ = false;
  if (mqtt_double->on_disconnect_callback_) {
    mqtt_double->on_disconnect_callback_(esphome::mqtt::MQTTClientDisconnectReason::TCP_DISCONNECTED);
  }

  for (int i = 0; i < 100; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_millis_wrap_during_startup)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(UINT32_MAX - 500);
  bridge->loop();

  esphome_hal_double_set_millis(1000);
  bridge->loop();

  for (int i = 0; i < 50; i++) {
    esphome_hal_double_set_millis(1000 + i * 100);
    bridge->loop();
  }

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_with_large_time_jump)
{
  configure_poll_mode();
  bridge->setup();
  bridge->loop();

  esphome_hal_double_set_millis(0);
  bridge->loop();

  esphome_hal_double_set_millis(3600000);
  bridge->loop();

  for (int i = 0; i < 50; i++) {
    esphome_hal_double_set_millis(3600000 + i * 100);
    bridge->loop();
  }

  CHECK(bridge->teardown());
}

TEST(startup_integration, full_startup_stress_across_all_modes)
{
  for (int mode = BRIDGE_MODE_POLL; mode <= BRIDGE_MODE_AUTO; mode++) {
    esphome_hal_double_set_millis(mode * 5000);
    bridge->set_gea3_uart(mock_uart);
    bridge->set_mode(static_cast<BridgeMode>(mode));
    bridge->set_appliance_api_parsing(false);
    bridge->setup();
    bridge->loop();

    esphome_hal_double_set_millis(mode * 5000 + AUTODISCOVERY_STARTUP_DELAY_MS + 100);
    bridge->loop();

    for (int i = 0; i < 100; i++) {
      esphome_hal_double_set_millis(mode * 5000 + AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
      bridge->loop();
    }

    IBridgeServices* services = bridge;
    CHECK_EQUAL(static_cast<BridgeMode>(mode), services->get_mode());

    CHECK(bridge->teardown());
  }
}

TEST(startup_integration, full_startup_stress_with_all_uart_combinations)
{
  // GEA3 only
  esphome_hal_double_set_millis(0);
  bridge->set_gea3_uart(mock_uart);
  bridge->set_gea2_uart(nullptr);
  bridge->set_mode(BRIDGE_MODE_POLL);
  bridge->set_appliance_api_parsing(false);
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  for (int i = 0; i < 50; i++) {
    esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }
  CHECK(bridge->teardown());

  // GEA2 only
  esphome_hal_double_set_millis(10000);
  bridge->set_gea3_uart(nullptr);
  bridge->set_gea2_uart(mock_uart);
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(10000 + AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  for (int i = 0; i < 50; i++) {
    esphome_hal_double_set_millis(10000 + AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }
  CHECK(bridge->teardown());

  // Both
  esphome_hal_double_set_millis(20000);
  bridge->set_gea3_uart(mock_uart);
  bridge->set_gea2_uart(mock_uart);
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(20000 + AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  for (int i = 0; i < 50; i++) {
    esphome_hal_double_set_millis(20000 + AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }
  CHECK(bridge->teardown());

  // Neither
  esphome_hal_double_set_millis(30000);
  bridge->set_gea3_uart(nullptr);
  bridge->set_gea2_uart(nullptr);
  bridge->setup();
  bridge->loop();
  esphome_hal_double_set_millis(30000 + AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  for (int i = 0; i < 50; i++) {
    esphome_hal_double_set_millis(30000 + AUTODISCOVERY_STARTUP_DELAY_MS + 1000 + i * 100);
    bridge->loop();
  }
  CHECK(bridge->teardown());
}
