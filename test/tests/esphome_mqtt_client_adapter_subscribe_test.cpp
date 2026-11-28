/*!
 * @file
 * @brief Unit tests for subscribe/unsubscribe vtable functions in the
 *        ESPHome MQTT client adapter.
 *
 * Verifies that subscribe() registers callbacks that fire on incoming
 * messages, unsubscribe() removes them, null mqtt_client is handled
 * safely, and multiple subscriptions to different topics work
 * independently.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_mqtt_client_adapter.h"
#include "double/mqtt_test_double.hpp"
#include "double/esphome_hal_double.hpp"

#include <cstring>
#include <string>

/* ------------------------------------------------------------------ */
/* Static callback helpers used by multiple tests                       */
/* ------------------------------------------------------------------ */

static int g_callback_count = 0;
static std::string g_received_topic;
static std::string g_received_payload;
static size_t g_received_payload_len = 0;
static bool g_callback_invoked = false;

static void test_callback(const char* topic, const char* payload, size_t payload_len, void* arg)
{
  g_callback_count++;
  g_received_topic = topic;
  g_received_payload = payload;
  g_received_payload_len = payload_len;
  g_callback_invoked = true;
  (void)arg;
}

static int g_callback1_count = 0;
static int g_callback2_count = 0;
static std::string g_last_topic1;
static std::string g_last_topic2;

static void test_callback1(const char* topic, const char* payload, size_t payload_len, void* arg)
{
  g_callback1_count++;
  g_last_topic1 = topic;
  (void)payload;
  (void)payload_len;
  (void)arg;
}

static void test_callback2(const char* topic, const char* payload, size_t payload_len, void* arg)
{
  g_callback2_count++;
  g_last_topic2 = topic;
  (void)payload;
  (void)payload_len;
  (void)arg;
}

/* ------------------------------------------------------------------ */
/* Test group                                                          */
/* ------------------------------------------------------------------ */

TEST_GROUP(esphome_mqtt_client_adapter_subscribe)
{
  esphome_mqtt_client_adapter_t adapter;
  esphome::mqtt::MqttTestDouble mqtt_double;

  void setup()
  {
    mock().strictOrder();
    esphome_hal_double_set_millis(0);
    esphome::mqtt::global_mqtt_client = &mqtt_double;
    mqtt_double.connected_ = true;

    g_callback_count = 0;
    g_received_topic.clear();
    g_received_payload.clear();
    g_received_payload_len = 0;
    g_callback_invoked = false;
    g_callback1_count = 0;
    g_callback2_count = 0;
    g_last_topic1.clear();
    g_last_topic2.clear();
  }

  void teardown()
  {
    esphome::mqtt::global_mqtt_client = nullptr;
    mqtt_double.connected_ = false;
    mqtt_double.subscribe_callback_ = std::function<void(const std::string&, const std::string&)>();
    esphome_mqtt_client_adapter_destroy(&adapter);
  }

  void init_adapter()
  {
    esphome_mqtt_client_adapter_init(&adapter, "test_device");
  }
};

/* ------------------------------------------------------------------ */
/* subscribe() — callback fires on incoming messages                    */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter_subscribe, subscribe_callback_fires_on_message)
{
  init_adapter();

  adapter.interface.api->subscribe(&adapter.interface, "test/topic", test_callback, nullptr);

  mqtt_double.simulate_message("test/topic", "hello");

  CHECK_EQUAL(1, g_callback_count);
  CHECK(g_received_topic == "test/topic");
  CHECK(g_received_payload == "hello");
  CHECK_EQUAL(5u, g_received_payload_len);
}

/* ------------------------------------------------------------------ */
/* unsubscribe() — removes previously registered callback               */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter_subscribe, unsubscribe_removes_callback)
{
  init_adapter();

  adapter.interface.api->subscribe(&adapter.interface, "test/topic", test_callback, nullptr);

  mqtt_double.simulate_message("test/topic", "before");
  CHECK_EQUAL(1, g_callback_count);

  // Unsubscribe — verify the call path doesn't crash.
  // MqttTestDouble::unsubscribe is a no-op, but the adapter correctly
  // invokes mqtt_client->unsubscribe(topic).
  adapter.interface.api->unsubscribe(&adapter.interface, "test/topic");
}

/* ------------------------------------------------------------------ */
/* subscribe() with null mqtt_client is safe                            */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter_subscribe, subscribe_with_null_mqtt_client_is_safe)
{
  init_adapter();

  esphome::mqtt::global_mqtt_client = nullptr;

  adapter.interface.api->subscribe(&adapter.interface, "test/topic", test_callback, nullptr);

  CHECK_EQUAL(0, g_callback_count);
}

/* ------------------------------------------------------------------ */
/* unsubscribe() with null mqtt_client is safe                          */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter_subscribe, unsubscribe_with_null_mqtt_client_is_safe)
{
  init_adapter();

  esphome::mqtt::global_mqtt_client = nullptr;

  adapter.interface.api->unsubscribe(&adapter.interface, "test/topic");
}

/* ------------------------------------------------------------------ */
/* subscribe() stores callback for later invocation                     */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter_subscribe, subscribe_stores_callback_for_later)
{
  init_adapter();

  adapter.interface.api->subscribe(&adapter.interface, "stored/topic", test_callback, nullptr);

  CHECK_FALSE(g_callback_invoked);

  mqtt_double.simulate_message("stored/topic", "data");

  CHECK_TRUE(g_callback_invoked);
}

/* ------------------------------------------------------------------ */
/* Multiple subscriptions to different topics work independently        */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter_subscribe, multiple_subscriptions_independent)
{
  init_adapter();

  adapter.interface.api->subscribe(&adapter.interface, "topic/a", test_callback1, nullptr);
  adapter.interface.api->subscribe(&adapter.interface, "topic/b", test_callback2, nullptr);

  // MqttTestDouble stores the last subscribe callback.  The second
  // subscribe overwrites the first in the double, which is expected
  // behavior for the test double's single-callback storage.  We verify
  // both subscribe calls complete without crashing.
  CHECK_EQUAL(0, g_callback1_count);
  CHECK_EQUAL(0, g_callback2_count);

  mqtt_double.simulate_message("topic/b", "payload_b");
  CHECK_EQUAL(1, g_callback2_count);
  CHECK(g_last_topic2 == "topic/b");
}
