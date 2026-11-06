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

#include <cstdio>
#include <string>

TEST_GROUP(write_flow_integration)
{
  esphome_mqtt_client_adapter_t adapter;
  erd_write_bridge_t write_bridge;
  erd_cache_t test_cache;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;

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

    erd_write_bridge_init(
      &write_bridge,
      &timer_group.timer_group,
      &erd_client.interface,
      &adapter.interface,
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

  /* Teardown calls mock().clear(), which silently discards unmet
   * expectations; fail here instead if the write never reached the client. */
  mock().checkExpectations();
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

  mock().checkExpectations();
}

TEST(write_flow_integration, should_drop_write_when_host_is_broadcast)
{
  init_all(tiny_gea_broadcast_address);

  tiny_erd_t erd = 0x7701;

  when_home_assistant_publishes_write(erd, "01");

  /* The real adapter's update_erd_write_result runs (it is not mocked
   * here), so assert on the ESPHome MQTT double's recorded publish — a
   * mock expectation on it would be silently discarded by teardown's
   * mock().clear(). */
  STRCMP_EQUAL("geappliances/test_device/erd/0x7701/write_result",
               mqtt_double.last_published_topic_.c_str());
  STRCMP_EQUAL("{\"error\":\"not_supported\"}", mqtt_double.last_published_payload_.c_str());
  CHECK(mqtt_double.last_published_retain_);
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

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);

  STRCMP_EQUAL("geappliances/test_device/erd/0x7701/write_result",
               mqtt_double.last_published_topic_.c_str());
  STRCMP_EQUAL("ok", mqtt_double.last_published_payload_.c_str());
  CHECK(mqtt_double.last_published_retain_);

  mock().checkExpectations();
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

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);

  STRCMP_EQUAL("geappliances/test_device/erd/0x7701/write_result",
               mqtt_double.last_published_topic_.c_str());
  STRCMP_EQUAL("{\"error\":\"incorrect_size\"}", mqtt_double.last_published_payload_.c_str());
  CHECK(mqtt_double.last_published_retain_);

  mock().checkExpectations();
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

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);

  STRCMP_EQUAL("geappliances/test_device/erd/0xa1_0x7701/write_result",
               mqtt_double.last_published_topic_.c_str());
  STRCMP_EQUAL("ok", mqtt_double.last_published_payload_.c_str());
  CHECK(mqtt_double.last_published_retain_);

  mock().checkExpectations();
}
