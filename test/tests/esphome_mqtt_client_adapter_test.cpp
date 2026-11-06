/*!
 * @file
 * @brief Unit tests for the ESPHome MQTT client adapter.
 *
 * The adapter publishes write_result topics through its own publish_raw
 * slot. These tests pin the golden topic strings (shared erd_topic
 * grammar) against the ESPHome MQTT double's recorded publish and verify
 * that the MQTT-specific entry points are safe with no client wired.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_mqtt_client_adapter.h"
#include "double/esphome_hal_double.hpp"
#include "double/mqtt_test_double.hpp"

#include <string>
#include <set>

/* ------------------------------------------------------------------ */
/* Test group                                                          */
/* ------------------------------------------------------------------ */

TEST_GROUP(esphome_mqtt_client_adapter)
{
  esphome_mqtt_client_adapter_t adapter;
  std::string device_id_str;

  void setup()
  {
    mock().strictOrder();
    device_id_str = "test_device";
    esphome_hal_double_set_millis(0);
  }

  void teardown()
  {
    esphome_mqtt_client_adapter_destroy(&adapter);
    mock().clear();
  }

  void init_adapter()
  {
    esphome_mqtt_client_adapter_init(&adapter, device_id_str.c_str());
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



/* ------------------------------------------------------------------ */
/* update_erd_write_result                                              */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, update_erd_write_result_success_publishes_ok_on_primary_topic)
{
  init_adapter();
  esphome::mqtt::MqttTestDouble mqtt_double;
  mqtt_double.connected_ = true;
  esphome::mqtt::global_mqtt_client = &mqtt_double;

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, 0xFF, true, 0);

  STRCMP_EQUAL("geappliances/test_device/erd/0x0092/write_result", mqtt_double.last_published_topic_.c_str());
  STRCMP_EQUAL("ok", mqtt_double.last_published_payload_.c_str());
  esphome::mqtt::global_mqtt_client = nullptr;
}

TEST(esphome_mqtt_client_adapter, update_erd_write_result_failure_publishes_error_on_secondary_topic)
{
  init_adapter();
  esphome::mqtt::MqttTestDouble mqtt_double;
  mqtt_double.connected_ = true;
  esphome::mqtt::global_mqtt_client = &mqtt_double;

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, 0xA1, false,
      tiny_gea3_erd_client_write_failure_reason_incorrect_size);

  STRCMP_EQUAL("geappliances/test_device/erd/0xa1_0x0092/write_result", mqtt_double.last_published_topic_.c_str());
  STRCMP_EQUAL("{\"error\":\"incorrect_size\"}", mqtt_double.last_published_payload_.c_str());
  esphome::mqtt::global_mqtt_client = nullptr;
}

TEST(esphome_mqtt_client_adapter, update_erd_write_result_with_no_mqtt_client_is_safe)
{
  init_adapter();
  esphome::mqtt::global_mqtt_client = nullptr;

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, 0xA1, false, 3);
}
/* ------------------------------------------------------------------ */
/* destroy                                                              */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, destroy_frees_resources)
{
  init_adapter();
  /* destroy happens in teardown */
}

/* ------------------------------------------------------------------ */
/* notify_connected / notify_disconnected — no-ops                      */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, notify_connected_is_noop)
{
  init_adapter();
  esphome_mqtt_client_adapter_notify_connected(&adapter);
  // Should not crash
}

TEST(esphome_mqtt_client_adapter, notify_disconnected_is_noop)
{
  init_adapter();
  esphome_mqtt_client_adapter_notify_disconnected(&adapter);
  // Should not crash
}

TEST(esphome_mqtt_client_adapter, subscribe_write_topic_is_noop)
{
  init_adapter();
  esphome_mqtt_client_adapter_subscribe_write_topic(&adapter);
  // Should not crash
}

/* ------------------------------------------------------------------ */
/* on_write_request / on_mqtt_disconnect event accessors                */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, on_write_request_returns_event)
{
  init_adapter();
  i_tiny_event_t* evt = adapter.interface.api->on_write_request(&adapter.interface);
  CHECK(evt != nullptr);
}

TEST(esphome_mqtt_client_adapter, on_mqtt_disconnect_returns_event)
{
  init_adapter();
  i_tiny_event_t* evt = adapter.interface.api->on_mqtt_disconnect(&adapter.interface);
  CHECK(evt != nullptr);
}
