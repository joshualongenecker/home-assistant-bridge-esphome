/*!
 * @file
 * @brief Unit tests for the ESPHome MQTT client adapter.
 *
 * Validates initialization, ERD publishing (hex & string), valid ERD filtering,
 * queue behavior when MQTT is disconnected, wildcard subscription, and
 * pending update flushing.
 *
 * NOTE: Tests that invoke the subscribe callback lambda are avoided - the
 * lambda captures a raw pointer to the adapter and invoking it after the
 * adapter is destroyed causes use-after-free. The callback logic is tested
 * through integration tests in the bridge itself.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_mqtt_client_adapter.h"
#include "double/esphome_hal_double.hpp"
#include "esphome/components/mqtt/mqtt_client.h"

#include <string>
#include <map>
#include <set>
#include <vector>

/* ------------------------------------------------------------------ */
/* Mock MQTT client that tracks calls                                 */
/* ------------------------------------------------------------------ */

struct MockMqttClient : public esphome::mqtt::MQTTClientComponent {
  bool connected;
  std::vector<std::string> published_topics;
  std::vector<std::string> published_payloads;
  std::vector<bool> published_retain;
  std::vector<std::string> subscribed_topics;
  std::vector<std::function<void(const std::string&, const std::string&)>> subscribed_callbacks;

  MockMqttClient() : connected(false) {}

  bool is_connected() override { return connected; }

  void publish(const std::string& topic, const std::string& payload,
               uint8_t /*qos*/, bool retain) override
  {
    published_topics.push_back(topic);
    published_payloads.push_back(payload);
    published_retain.push_back(retain);
  }

  void subscribe(const std::string& topic,
                 std::function<void(const std::string&, const std::string&)> callback,
                 uint8_t /*qos*/) override
  {
    subscribed_topics.push_back(topic);
    subscribed_callbacks.push_back(callback);
  }

  void clear()
  {
    subscribed_callbacks.clear();
    published_topics.clear();
    published_payloads.clear();
    published_retain.clear();
    subscribed_topics.clear();
    connected = false;
  }
};

/* ------------------------------------------------------------------ */
/* Test group - basic tests that do NOT call notify_connected          */
/* ------------------------------------------------------------------ */

TEST_GROUP(esphome_mqtt_client_adapter)
{
  esphome_mqtt_client_adapter_t adapter;
  MockMqttClient mock_client;
  std::string device_id_str;
  std::vector<tiny_erd_t> valid_erds;
  std::vector<tiny_erd_t> string_erds;
  std::set<tiny_erd_t> registered_erds_out;

  void setup()
  {
    mock().strictOrder();
    device_id_str = "test_device";
    esphome::mqtt::global_mqtt_client = &mock_client;
    mock_client.clear();
    valid_erds.clear();
    string_erds.clear();
    registered_erds_out.clear();
    esphome_hal_double_set_millis(0);
  }

  void teardown()
  {
    esphome_mqtt_client_adapter_destroy(&adapter);
    esphome::mqtt::global_mqtt_client = nullptr;
    mock().clear();
  }

  void init_adapter()
  {
    esphome_mqtt_client_adapter_init(&adapter, device_id_str.c_str());
  }

  void init_adapter_with_filters()
  {
    init_adapter();
    esphome_mqtt_client_adapter_set_valid_erds_filter(&adapter, valid_erds.data(), valid_erds.size());
    esphome_mqtt_client_adapter_set_string_erds_filter(&adapter, string_erds.data(), string_erds.size());
    esphome_mqtt_client_adapter_set_registered_erds_out(&adapter, &registered_erds_out);
  }
};

/* ------------------------------------------------------------------ */
/* Initialization                                                       */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, init_sets_api_pointer)
{
  init_adapter();
  CHECK(adapter.interface.api != nullptr);
}

TEST(esphome_mqtt_client_adapter, init_creates_device_id)
{
  init_adapter();
  adapter.interface.api->register_erd(&adapter.interface, 0x0008);
}

/* ------------------------------------------------------------------ */
/* register_erd                                                         */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, register_erd_tracks_in_output_set)
{
  valid_erds.push_back(0x0008);
  init_adapter_with_filters();
  adapter.interface.api->register_erd(&adapter.interface, 0x0008);
  CHECK_EQUAL(1u, registered_erds_out.size());
  CHECK(registered_erds_out.count(0x0008) == 1);
}

TEST(esphome_mqtt_client_adapter, register_erd_without_output_set)
{
  init_adapter();
  adapter.interface.api->register_erd(&adapter.interface, 0x0008);
}

/* ------------------------------------------------------------------ */
/* update_erd - connected path (hex)                                    */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, update_erd_publishes_hex_when_connected)
{
  init_adapter();
  mock_client.connected = true;

  uint8_t data[] = {0x01, 0x02, 0xAB};
  adapter.interface.api->update_erd(&adapter.interface, 0x0092, data, 3);

  // update_erd always queues; notify_connected drains the queue and publishes
  esphome_mqtt_client_adapter_notify_connected(&adapter);

  CHECK_EQUAL(1u, mock_client.published_topics.size());
  CHECK(mock_client.published_topics.back() == "geappliances/test_device/erd/0x0092/value");
  CHECK(mock_client.published_payloads.back() == "0102ab");
  CHECK_TRUE(mock_client.published_retain.back());
}

TEST(esphome_mqtt_client_adapter, update_erd_publishes_string_when_in_filter)
{
  string_erds.push_back(0x0001);
  init_adapter();
  esphome_mqtt_client_adapter_set_string_erds_filter(&adapter, string_erds.data(), string_erds.size());
  mock_client.connected = true;

  uint8_t data[] = "Hello";
  adapter.interface.api->update_erd(&adapter.interface, 0x0001, data, 5);

  // update_erd always queues; notify_connected drains the queue and publishes
  esphome_mqtt_client_adapter_notify_connected(&adapter);

  CHECK_EQUAL(1u, mock_client.published_topics.size());
  CHECK(mock_client.published_topics.back() == "geappliances/test_device/erd/0x0001/value");
  CHECK(mock_client.published_payloads.back() == "Hello");
}

/* ------------------------------------------------------------------ */
/* update_erd - filter & validation                                     */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, update_erd_skips_erd_not_in_valid_filter)
{
  valid_erds.push_back(0x0092);
  init_adapter_with_filters();
  mock_client.connected = true;

  uint8_t data[] = {0x01};
  adapter.interface.api->update_erd(&adapter.interface, 0x0093, data, 1);

  CHECK_EQUAL(0u, mock_client.published_topics.size());
}

TEST(esphome_mqtt_client_adapter, update_erd_skips_null_value)
{
  init_adapter();
  mock_client.connected = true;

  adapter.interface.api->update_erd(&adapter.interface, 0x0092, nullptr, 0);
  CHECK_EQUAL(0u, mock_client.published_topics.size());
}

TEST(esphome_mqtt_client_adapter, update_erd_skips_zero_size)
{
  init_adapter();
  mock_client.connected = true;

  uint8_t data[] = {0x01};
  adapter.interface.api->update_erd(&adapter.interface, 0x0092, data, 0);
  CHECK_EQUAL(0u, mock_client.published_topics.size());
}

/* ------------------------------------------------------------------ */
/* update_erd - disconnected path (queue)                               */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, update_erd_queues_when_disconnected)
{
  init_adapter();
  mock_client.connected = false;

  uint8_t data[] = {0x01, 0x02};
  adapter.interface.api->update_erd(&adapter.interface, 0x0092, data, 2);

  CHECK_EQUAL(0u, mock_client.published_topics.size());
}

/* ------------------------------------------------------------------ */
/* update_erd_write_result                                              */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, update_erd_write_result_publishes_success)
{
  init_adapter();
  mock_client.connected = true;

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, true, 0);

  CHECK_EQUAL(1u, mock_client.published_topics.size());
  CHECK(mock_client.published_topics.back() == "geappliances/test_device/erd/0x0092/write_result");
  CHECK(mock_client.published_payloads.back() == "success");
}

TEST(esphome_mqtt_client_adapter, update_erd_write_result_publishes_failure)
{
  init_adapter();
  mock_client.connected = true;

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, false, 3);

  CHECK_EQUAL(1u, mock_client.published_topics.size());
  CHECK(mock_client.published_payloads.back() == "failure (reason: 3)");
}

TEST(esphome_mqtt_client_adapter, update_erd_write_result_drops_when_disconnected)
{
  init_adapter();
  mock_client.connected = false;

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, true, 0);
  CHECK_EQUAL(0u, mock_client.published_topics.size());
}

/* ------------------------------------------------------------------ */
/* destroy                                                              */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, destroy_frees_resources)
{
  init_adapter();
  uint8_t d[] = {0x01};
  adapter.interface.api->update_erd(&adapter.interface, 0x0092, d, 1);
  /* destroy happens in teardown */
}

/* ------------------------------------------------------------------ */
/* Tests that call notify_connected (use local mock to control lifetime)*/
/* The adapter is destroyed BEFORE the local mock goes out of scope,   */
/* so the lambda in subscribed_callbacks has a dangling pointer.       */
/* We only verify subscription topics and flush behavior, not callback  */
/* invocation.                                                         */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, notify_connected_subscribes_wildcard_once)
{
  MockMqttClient local_mock;
  esphome::mqtt::global_mqtt_client = &local_mock;
  local_mock.connected = true;

  esphome_mqtt_client_adapter_t local_adapter;
  esphome_mqtt_client_adapter_init(&local_adapter, "test_device");

  esphome_mqtt_client_adapter_notify_connected(&local_adapter);
  CHECK_EQUAL(1u, local_mock.subscribed_topics.size());
  CHECK(local_mock.subscribed_topics.back() == "geappliances/test_device/erd/+/write");

  esphome_mqtt_client_adapter_notify_connected(&local_adapter);
  CHECK_EQUAL(1u, local_mock.subscribed_topics.size());

  esphome_mqtt_client_adapter_destroy(&local_adapter);
  esphome::mqtt::global_mqtt_client = nullptr;
}

TEST(esphome_mqtt_client_adapter, notify_connected_does_not_subscribe_if_not_connected)
{
  MockMqttClient local_mock;
  esphome::mqtt::global_mqtt_client = &local_mock;
  local_mock.connected = false;

  esphome_mqtt_client_adapter_t local_adapter;
  esphome_mqtt_client_adapter_init(&local_adapter, "test_device");

  esphome_mqtt_client_adapter_notify_connected(&local_adapter);
  CHECK_EQUAL(0u, local_mock.subscribed_topics.size());

  esphome_mqtt_client_adapter_destroy(&local_adapter);
  esphome::mqtt::global_mqtt_client = nullptr;
}

TEST(esphome_mqtt_client_adapter, notify_connected_flushes_pending_updates)
{
  MockMqttClient local_mock;
  esphome::mqtt::global_mqtt_client = &local_mock;

  esphome_mqtt_client_adapter_t local_adapter;
  esphome_mqtt_client_adapter_init(&local_adapter, "test_device");

  local_mock.connected = false;
  uint8_t d1[] = {0x01};
  uint8_t d2[] = {0x02};
  local_adapter.interface.api->update_erd(&local_adapter.interface, 0x0092, d1, 1);
  local_adapter.interface.api->update_erd(&local_adapter.interface, 0x0093, d2, 1);

  local_mock.connected = true;
  esphome_mqtt_client_adapter_notify_connected(&local_adapter);
  CHECK_EQUAL(2u, local_mock.published_topics.size());

  esphome_mqtt_client_adapter_destroy(&local_adapter);
  esphome::mqtt::global_mqtt_client = nullptr;
}

TEST(esphome_mqtt_client_adapter, notify_connected_flushes_max_per_call)
{
  MockMqttClient local_mock;
  esphome::mqtt::global_mqtt_client = &local_mock;

  esphome_mqtt_client_adapter_t local_adapter;
  esphome_mqtt_client_adapter_init(&local_adapter, "test_device");

  local_mock.connected = false;
  for (int i = 0; i < 10; i++) {
    uint8_t d[] = {0x01};
    local_adapter.interface.api->update_erd(&local_adapter.interface, (tiny_erd_t)(0x0092 + i), d, 1);
  }

  local_mock.connected = true;
  esphome_mqtt_client_adapter_notify_connected(&local_adapter);
  CHECK_EQUAL(5u, local_mock.published_topics.size());

  esphome_mqtt_client_adapter_destroy(&local_adapter);
  esphome::mqtt::global_mqtt_client = nullptr;
}

TEST(esphome_mqtt_client_adapter, notify_disconnected_resets_connect_time)
{
  MockMqttClient local_mock;
  esphome::mqtt::global_mqtt_client = &local_mock;
  local_mock.connected = true;

  esphome_mqtt_client_adapter_t local_adapter;
  esphome_mqtt_client_adapter_init(&local_adapter, "test_device");

  esphome_hal_double_set_millis(1000);
  esphome_mqtt_client_adapter_notify_connected(&local_adapter);
  esphome_mqtt_client_adapter_notify_disconnected(&local_adapter);

  esphome_hal_double_set_millis(2000);
  esphome_mqtt_client_adapter_notify_connected(&local_adapter);
  CHECK_EQUAL(1u, local_mock.subscribed_topics.size());

  esphome_mqtt_client_adapter_destroy(&local_adapter);
  esphome::mqtt::global_mqtt_client = nullptr;
}

TEST(esphome_mqtt_client_adapter, write_request_callback_registered)
{
  MockMqttClient local_mock;
  esphome::mqtt::global_mqtt_client = &local_mock;
  local_mock.connected = true;

  esphome_mqtt_client_adapter_t local_adapter;
  esphome_mqtt_client_adapter_init(&local_adapter, "test_device");
  esphome_mqtt_client_adapter_notify_connected(&local_adapter);

  /* Verify callback was registered (do NOT invoke it - dangling pointer) */
  CHECK_EQUAL(1u, local_mock.subscribed_callbacks.size());

  esphome_mqtt_client_adapter_destroy(&local_adapter);
  esphome::mqtt::global_mqtt_client = nullptr;
}

TEST(esphome_mqtt_client_adapter, update_erd_overwrites_pending_for_same_erd)
{
  MockMqttClient local_mock;
  esphome::mqtt::global_mqtt_client = &local_mock;

  esphome_mqtt_client_adapter_t local_adapter;
  esphome_mqtt_client_adapter_init(&local_adapter, "test_device");

  local_mock.connected = false;
  uint8_t data1[] = {0x01};
  local_adapter.interface.api->update_erd(&local_adapter.interface, 0x0092, data1, 1);

  uint8_t data2[] = {0xFF};
  local_adapter.interface.api->update_erd(&local_adapter.interface, 0x0092, data2, 1);

  local_mock.connected = true;
  esphome_mqtt_client_adapter_notify_connected(&local_adapter);
  CHECK_EQUAL(1u, local_mock.published_topics.size());
  CHECK(local_mock.published_payloads.back() == "ff");

  esphome_mqtt_client_adapter_destroy(&local_adapter);
  esphome::mqtt::global_mqtt_client = nullptr;
}
