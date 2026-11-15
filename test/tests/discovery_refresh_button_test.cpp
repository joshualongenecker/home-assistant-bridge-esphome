/*!
 * @file
 * @brief Unit tests for DiscoveryRefreshButton.
 *
 * Tests construction, press_action() behavior under various bridge states,
 * and null-bridge safety.
 *
 * Note: Production headers with STL containers (<string>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "geappliances_bridge.h"
#include "button/button.h"
#include "CppUTest/TestHarness.h"

using namespace esphome::geappliances_bridge;

TEST_GROUP(discovery_refresh_button)
{
  GeappliancesBridge bridge;

  void setup()
  {
    // Each test gets a fresh bridge with default member values.
    // data_plane_readiness_ = kNotSteady, the lifecycle phase machine is
    // idle, and ha_discovery_manager_ is zero-initialized.
  }
};

TEST(discovery_refresh_button, can_be_constructed_with_bridge_pointer)
{
  DiscoveryRefreshButton button(&bridge);
  /* Button constructed successfully — no crash. */
  (void)button;
}
TEST(discovery_refresh_button, press_action_does_not_crash_with_uninitialized_bridge)
{
  /* Bridge has not been set up (data_plane_readiness_ is kNotSteady).
   * trigger_discovery_refresh() claims the phase machine regardless; the
   * publish only runs once the bridge reports steady state. */
  DiscoveryRefreshButton button(&bridge);
  button.press_action();
  /* No crash = pass. */
}
TEST(discovery_refresh_button, press_action_is_idempotent_after_successful_trigger)
{
  /* The phase machine serializes requests itself: a second claim while a
   * run is active is rejected. Verify that calling press_action() multiple
   * times is safe. */
  DiscoveryRefreshButton button(&bridge);
  button.press_action();
  button.press_action();
  button.press_action();
  /* Multiple presses must not crash regardless of internal state. */
}
TEST(discovery_refresh_button, press_action_safe_when_bridge_is_null)
{
  /* Button constructed with null bridge pointer must not crash on press. */
  DiscoveryRefreshButton button(nullptr);
  button.press_action();
  /* No crash = pass. */
}
TEST(discovery_refresh_button, multiple_presses_with_null_bridge_are_safe)
{
  DiscoveryRefreshButton button(nullptr);
  button.press_action();
  button.press_action();
  button.press_action();
  /* No crash = pass. */
}

TEST(discovery_refresh_button, button_inherits_from_button_base)
{
  // Verify the button is a proper subclass of esphome::button::Button.
  DiscoveryRefreshButton button(&bridge);
  esphome::button::Button* base = &button;
  CHECK(base != nullptr);
}

TEST(discovery_refresh_button, press_action_safe_after_teardown)
{
  /* Calling teardown() on the bridge and then pressing the button must not crash. */
  DiscoveryRefreshButton button(&bridge);
  bridge.teardown();
  button.press_action();
  /* No crash = pass. */
}