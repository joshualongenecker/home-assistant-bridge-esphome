/*!
 * @file
 * @brief Unit tests for ReconnectApplianceButton.
 *
 * A press only requests the restart; the bridge runs it at the start of its
 * next loop() (see startup_integration's restart tests for what the restart
 * does). These cover construction, presses in any bridge state, and null
 * safety.
 *
 * Note: Production headers with STL containers (<string>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "geappliances_bridge.h"
#include "button/button.h"
#include "CppUTest/TestHarness.h"

using namespace esphome::geappliances_bridge;

TEST_GROUP(reconnect_appliance_button)
{
  GeappliancesBridge bridge;
};

TEST(reconnect_appliance_button, can_be_constructed_with_bridge_pointer)
{
  ReconnectApplianceButton button(&bridge);
  (void)button;
}

TEST(reconnect_appliance_button, press_before_setup_does_not_crash)
{
  ReconnectApplianceButton button(&bridge);
  button.press_action();
}

TEST(reconnect_appliance_button, repeated_presses_are_safe)
{
  ReconnectApplianceButton button(&bridge);
  button.press_action();
  button.press_action();
  button.press_action();
}

TEST(reconnect_appliance_button, press_is_safe_when_bridge_is_null)
{
  ReconnectApplianceButton button(nullptr);
  button.press_action();
}
