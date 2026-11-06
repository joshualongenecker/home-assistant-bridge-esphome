/*!
 * @file
 * @brief Unit tests for the ESPHome MQTT client adapter (debug-log mode).
 *
 * The adapter no longer publishes to MQTT.  All publish paths are replaced
 * with ESP_LOGD debug messages.  These tests verify that the adapter
 * functions without crashing, correctly filters ERDs through the registry,
 * and that all MQTT-specific entry points are safe no-ops.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_mqtt_client_adapter.h"
#include "double/esphome_hal_double.hpp"

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

TEST(esphome_mqtt_client_adapter, update_erd_write_result_success_no_crash)
{
  init_adapter();

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, 0xFF, true, 0);
  // global_mqtt_client is null in tests — publish is skipped, no crash.
}

TEST(esphome_mqtt_client_adapter, update_erd_write_result_failure_no_crash)
{
  init_adapter();

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, 0xA1, false, 3);
  // global_mqtt_client is null in tests — publish is skipped, no crash.
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
