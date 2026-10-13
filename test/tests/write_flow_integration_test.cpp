/*!
 * @file
 * @brief Integration test: MQTT write topic -> adapter -> write bridge -> ERD client.
 *
 * Verifies the complete path from an incoming MQTT write message through the
 * adapter's wildcard subscription, into the write bridge, and finally to the
 * ERD client's write() call.
 */

extern "C" {
#include "erd_write_bridge.h"
#include "erd_cache.h"
#include "tiny_gea_constants.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_mqtt_client_adapter.h"
#include "double/mqtt_test_double.hpp"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"
#include "double/esphome_hal_double.hpp"
#include "erd_registry.h"

#include <cstdio>
#include <string>

TEST_GROUP(write_flow_integration)
{
  esphome_mqtt_client_adapter_t adapter;
  erd_write_bridge_t write_bridge;
  erd_cache_t test_cache;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  esphome::geappliances_bridge::ErdRegistry erd_registry;

  esphome::mqtt::MqttTestDouble mqtt_double;

  void setup()
  {
    mock().strictOrder();
    esphome_hal_double_set_millis(0);

    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);

    esphome::mqtt::global_mqtt_client = &mqtt_double;
    mqtt_double.connected_ = true;
  }

  void teardown()
  {
    esphome::mqtt::global_mqtt_client = nullptr;
    mqtt_double.connected_ = false;

    erd_write_bridge_destroy(&write_bridge);
    erd_cache_destroy(&test_cache);

    esphome_mqtt_client_adapter_destroy(&adapter);
    mock().clear();
  }

  void init_all(uint8_t host_address)
  {
    mock().disable();

    esphome_mqtt_client_adapter_init(&adapter, "test_device");
    esphome_mqtt_client_adapter_set_erd_registry(&adapter, &erd_registry);

    erd_write_bridge_init(
      &write_bridge,
      &timer_group.timer_group,
      &erd_client.interface,
      &adapter.interface,
      nullptr,
      host_address);

    esphome_mqtt_client_adapter_subscribe_write_topic(&adapter);

    mock().enable();
  }

  void when_home_assistant_publishes_write(tiny_erd_t erd, const std::string& payload)
  {
    char topic[128];
    snprintf(topic, sizeof(topic), "geappliances/test_device/erd/0x%04x/write", erd);
    mqtt_double.simulate_message(topic, payload);
  }

  void init_all_with_cache(uint8_t host_address)
  {
    mock().disable();

    esphome_mqtt_client_adapter_init(&adapter, "test_device");
    esphome_mqtt_client_adapter_set_erd_registry(&adapter, &erd_registry);

    erd_write_bridge_init(
      &write_bridge,
      &timer_group.timer_group,
      &erd_client.interface,
      &adapter.interface,
      &test_cache,
      host_address);

    esphome_mqtt_client_adapter_subscribe_write_topic(&adapter);

    mock().enable();
  }

  void when_home_assistant_publishes_field_write(tiny_erd_t erd, const std::string& payload,
    const std::string& field_suffix)
  {
    char topic[160];
    snprintf(topic, sizeof(topic), "geappliances/test_device/erd/0x%04x/write/%s",
      erd, field_suffix.c_str());
    mqtt_double.simulate_message(topic, payload);
  }
};

TEST(write_flow_integration, should_route_write_from_mqtt_to_erd_client)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "01");
}

TEST(write_flow_integration, should_parse_erd_from_topic_path)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0xFF;
  tiny_erd_t erd = 0x0092;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "FF");
}

TEST(write_flow_integration, should_drop_write_when_host_is_broadcast)
{
  init_all(tiny_gea_broadcast_address);

  tiny_erd_t erd = 0x7701;

  mock()
    .expectOneCall("update_erd_write_result")
    .onObject(&adapter.interface)
    .withParameter("erd", erd)
    .withParameter("board_address", 0xFF)
    .withParameter("success", false)
    .withParameter("failure_reason",
      tiny_gea3_erd_client_write_failure_reason_not_supported);

  when_home_assistant_publishes_write(erd, "01");
}

TEST(write_flow_integration, should_report_write_result_on_completion)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "01");

  // Simulate write completion.
  uint8_t dummy = 0;
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_write_completed;
  args.address = host;
  args.write_completed.request_id = mock_request_id;
  args.write_completed.erd = erd;
  args.write_completed.data = &dummy;
  args.write_completed.data_size = 1;

  mock()
    .expectOneCall("publish_raw")
    .withParameter("topic", "geappliances/test_device/erd/0x7701/write_result")
    .withParameterOfType("const char*", "payload", "ok")
    .withParameter("payload_len", 2)
    .withParameter("retain", true);

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
}

TEST(write_flow_integration, should_report_write_failure_to_mqtt)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "01");

  // Simulate write failure.
  uint8_t dummy = 0;
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_write_failed;
  args.address = host;
  args.write_failed.request_id = mock_request_id;
  args.write_failed.erd = erd;
  args.write_failed.data = &dummy;
  args.write_failed.data_size = 1;
  args.write_failed.reason = tiny_gea3_erd_client_write_failure_reason_incorrect_size;

  mock()
    .expectOneCall("publish_raw")
    .withParameter("topic", "geappliances/test_device/erd/0x7701/write_result")
    .withParameter("retain", true);

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
}

TEST(write_flow_integration, should_route_secondary_board_write_to_secondary_address)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  uint8_t secondary = 0xA1;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  // The write is published on the secondary-board topic form.
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", secondary)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  char topic[128];
  snprintf(topic, sizeof(topic), "geappliances/test_device/erd/0x%02x_0x%04x/write", secondary, erd);
  mqtt_double.simulate_message(topic, "01");

  // Simulate write completion; the result must go to the secondary-board topic.
  uint8_t dummy = 0;
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_write_completed;
  args.address = secondary;
  args.write_completed.request_id = mock_request_id;
  args.write_completed.erd = erd;
  args.write_completed.data = &dummy;
  args.write_completed.data_size = 1;

  mock()
    .expectOneCall("publish_raw")
    .withParameter("topic", "geappliances/test_device/erd/0xa1_0x7701/write_result")
    .withParameterOfType("const char*", "payload", "ok")
    .withParameter("payload_len", 2)
    .withParameter("retain", true);

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
}

// --- Field-write topic parsing (adapter -> write bridge RMW) ---
// Uses the generated RMW table's 0x7051 -> 0x7053 (1-byte) matched pair.

TEST(write_flow_integration, should_parse_field_write_topic_and_rmw)
{
  uint8_t host = 0xC0;
  init_all_with_cache(host);

  // Cache the base (0x7053) with a known value.
  uint8_t base = 0x00;
  erd_cache_update(&test_cache, 0x7053, host, &base, sizeof(base));

  tiny_gea3_erd_client_request_id_t mock_request_id{1};
  // Field: byte 0, bit 2, 1 bit. Value 0x01 (set) -> 0x00 | (1<<2) = 0x04.
  uint8_t expected = 0x04;
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", 0x7051)
    .withMemoryBufferParameter("data", &expected, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_field_write(0x7051, "01", "0/1/2/1");
}

TEST(write_flow_integration, should_parse_full_write_topic_unchanged)
{
  uint8_t host = 0xC0;
  init_all_with_cache(host);

  uint8_t value = 0x42;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", 0x7701)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  // A full-write topic (no field suffix) is forwarded verbatim.
  when_home_assistant_publishes_write(0x7701, "42");
}

TEST(write_flow_integration, should_drop_malformed_field_write_topic)
{
  uint8_t host = 0xC0;
  init_all_with_cache(host);

  // Cache the base so that IF the adapter failed to drop the topic, the write
  // bridge would proceed and call erd_client.write() — an unexpected mock call
  // that fails the test. With the adapter correct, no write is queued.
  uint8_t base = 0x00;
  erd_cache_update(&test_cache, 0x7053, host, &base, sizeof(base));

  // Three segments (not four) -> malformed, dropped. No write, no result.
  mqtt_double.simulate_message(
    "geappliances/test_device/erd/0x7051/write/0/1/2", "01");
}

TEST(write_flow_integration, should_drop_out_of_range_field_write_topic)
{
  uint8_t host = 0xC0;
  init_all_with_cache(host);

  // Cache the base so a regression that lets the topic through would queue a
  // write (unexpected mock call -> test failure).
  uint8_t base = 0x00;
  erd_cache_update(&test_cache, 0x7053, host, &base, sizeof(base));

  // byte_offset (300) exceeds the max payload size -> dropped by the adapter.
  mqtt_double.simulate_message(
    "geappliances/test_device/erd/0x7051/write/300/1/0/8", "01");
}

TEST(write_flow_integration, should_drop_overflow_field_write_topic)
{
  uint8_t host = 0xC0;
  init_all_with_cache(host);

  // Cache the base so a regression that lets the topic through would queue a
  // write (unexpected mock call -> test failure).
  uint8_t base = 0x00;
  erd_cache_update(&test_cache, 0x7053, host, &base, sizeof(base));

  // byte_offset huge enough to overflow the int range check (sscanf clamps to
  // INT_MAX; off + size wraps negative). Must be dropped, not written OOB.
  mqtt_double.simulate_message(
    "geappliances/test_device/erd/0x7051/write/99999999999999/1/0/8", "01");
}

TEST(write_flow_integration, should_parse_secondary_board_field_write)
{
  uint8_t host = 0xC0;
  uint8_t secondary = 0xA1;
  init_all_with_cache(host);

  // Cache the base on the secondary board.
  uint8_t base = 0x00;
  erd_cache_update(&test_cache, 0x7053, secondary, &base, sizeof(base));

  tiny_gea3_erd_client_request_id_t mock_request_id{1};
  uint8_t expected = 0x04;
  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", secondary)
    .withParameter("erd", 0x7051)
    .withMemoryBufferParameter("data", &expected, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  char topic[160];
  snprintf(topic, sizeof(topic), "geappliances/test_device/erd/0x%02x_0x7051/write/0/1/2/1", secondary);
  mqtt_double.simulate_message(topic, "01");
}
