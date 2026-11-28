// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Define the BridgeMode enumeration as a standalone, shared type so
//       it can be referenced by both IBridgeServices and GeappliancesBridge
//       without creating a circular dependency.
//
// Responsibilities:
//   - Declare BridgeMode and its three values (POLL, SUBSCRIBE, AUTO)
//   - Provide the mapping comment that ties values to __init__.py constants
//
// NOT responsible for:
//   - Any logic, state, or behaviour
//
// Dependencies:
//   - none
// =============================================================================

#pragma once

namespace esphome {
namespace geappliances_bridge {

enum BridgeMode {
  BRIDGE_MODE_POLL      = 0,
  BRIDGE_MODE_SUBSCRIBE = 1,
  BRIDGE_MODE_AUTO      = 2
};

}
}
