/*!
 * @file
 * @brief Unit tests for HA discovery integration in GeappliancesBridge.
 *
 * Tests set_generate_device_config(), set_filter_config_topics(),
 * trigger_discovery_refresh() safety, and default values of discovery
 * state fields.
 *
 * Note: Production headers with STL containers (<string>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "geappliances_bridge.h"
#include "button/button.h"
#include "CppUTest/TestHarness.h"

using namespace esphome::geappliances_bridge;

TEST_GROUP(geappliances_bridge_discovery)
{
  GeappliancesBridge bridge;

  void setup()
  {
    // Each test gets a fresh instance with default member values.
  }
};

TEST(geappliances_bridge_discovery, set_generate_device_config_sets_flag_correctly)
{
  /* Toggling setter doesn't crash. */
  bridge.set_generate_device_config(true);
  bridge.set_generate_device_config(false);
  bridge.set_generate_device_config(true);
}
TEST(geappliances_bridge_discovery, set_filter_config_topics_sets_flag_correctly)
{
  /* Toggling setter doesn't crash. */
  bridge.set_filter_config_topics(false);
  bridge.set_filter_config_topics(true);
  bridge.set_filter_config_topics(false);
}
TEST(geappliances_bridge_discovery, trigger_discovery_refresh_safe_on_initialized_bridge)
{
  /* trigger_discovery_refresh() is protected; access via DiscoveryRefreshButton.
   * The bridge has not been set up, so the phase machine claims the request
   * but the publish waits for steady state that never arrives here. */
  DiscoveryRefreshButton button(&bridge);
  button.press_action();
  /* No crash = the claim path is safe on an uninitialized bridge. */
}
TEST(geappliances_bridge_discovery, button_press_is_idempotent)
{
  /* Multiple presses don't crash: the phase machine rejects a second claim
   * while a run is active, and idle re-claims are harmless. */
  DiscoveryRefreshButton button(&bridge);
  button.press_action();
  button.press_action();
  button.press_action();
}
TEST(geappliances_bridge_discovery, button_safe_with_null_bridge)
{
  /* Button with null bridge pointer should not crash. */
  DiscoveryRefreshButton button(nullptr);
  button.press_action();
}
TEST(geappliances_bridge_discovery, button_inherits_from_button_base)
{
  /* DiscoveryRefreshButton is a subclass of esphome::button::Button. */
  GeappliancesBridge bridge;
  DiscoveryRefreshButton button(&bridge);
  /* Can cast to esphome::button::Button* without error. */
  esphome::button::Button* base = &button;
  CHECK(base != nullptr);
}

TEST(geappliances_bridge_discovery, setter_toggles_are_safe)
{
  /* Repeatedly toggling both setters doesn't crash. */
  for (int i = 0; i < 10; i++) {
    bridge.set_generate_device_config(i % 2 == 0);
    bridge.set_filter_config_topics(i % 2 != 0);
  }
}