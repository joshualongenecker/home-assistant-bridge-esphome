/*!
 * @file
 * @brief Startup state machine implementation.
 *
 * Each state function handles entry/exit signals and the signal_run_loop
 * signal that drives ongoing work.  Transitions to the next state happen
 * when the required gate conditions are met (e.g., autodiscovery complete,
 * device ID ready, MQTT connected, etc.).
 *
 * The hierarchy is flat — all states defer to startup_state_top for signals
 * they don't handle, providing a common "root" for any unhandled signals.
 */

#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "geappliances_bridge_startup_hsm.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

extern "C" {
#include "tiny_utils.h"  // element_count macro
}

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "geappliances_bridge";

// Back-pointer to the bridge instance, set during HSM init.
// This avoids using container_of (which relies on offsetof) on a
// non-POD C++ class — offsetof within non-standard-layout types is
// conditionally-supported and triggers compiler warnings/errors.
static GeappliancesBridge* g_bridge_instance = nullptr;

GeappliancesBridge* bridge_from_hsm(tiny_hsm_t* hsm)
{
  (void)hsm;
  return g_bridge_instance;
}

void set_bridge_instance(GeappliancesBridge* bridge)
{
  g_bridge_instance = bridge;
}


// ============================================================================
// Top state — handles signals common to all startup phases
// ============================================================================

tiny_hsm_result_t startup_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)data;
  (void)hsm;

  // The top state is the root of the hierarchy.  Any signal not consumed
  // by a child state will bubble up here and be deferred (ignored).
  switch (signal) {
    case tiny_hsm_signal_entry:
    case tiny_hsm_signal_exit:
      // No common entry/exit logic needed.
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 1: Protocol Stack — drive GEA2/GEA3 hardware
//
// This is the initial state.  It transitions to autodiscovery as soon as
// the first loop() call arrives (the protocol stack is always running).
// ============================================================================

tiny_hsm_result_t startup_state_protocol_stack(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)bridge;
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      // Always transition to autodiscovery on entry — the protocol stack
      // is driven from run_protocol_stack_() which runs before the HSM.
      tiny_hsm_transition(hsm, startup_state_autodiscovery);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 2: Autodiscovery — find appliance on bus
//
// Runs the AutodiscoveryManager each loop iteration.  Transitions to
// device_id only when autodiscovery successfully discovers a board.
// If no board responds, the manager keeps retrying indefinitely — this
// state will not transition until a valid board address is found.
// ============================================================================

tiny_hsm_result_t startup_state_autodiscovery(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Autodiscovery phase");
      break;

    case signal_run_loop:
      bridge->autodiscovery_manager_.run();

      // Only transition when autodiscovery successfully discovers a board.
      // If no board responds, the manager keeps retrying indefinitely.
      if (bridge->autodiscovery_manager_.is_complete()) {
        ESP_LOGI(TAG, "Autodiscovery complete (host=0x%02X, protocol=%s)",
                 bridge->autodiscovery_manager_.get_host_address(),
                 bridge->autodiscovery_manager_.is_gea2_protocol() ? "GEA2" : "GEA3");
        tiny_hsm_transition(hsm, startup_state_device_id);
      }
      break;

    case signal_autodiscovery_complete:
      // External signal from autodiscovery callback — transition only if
      // a board was actually discovered (not on failure/no-response).
      if (bridge->autodiscovery_manager_.is_complete()) {
        tiny_hsm_transition(hsm, startup_state_device_id);
      }
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 3: Device ID — read appliance identity ERDs
//
// Runs the DeviceIdentityManager each loop iteration.  Transitions to
// mqtt_client_init when the device ID is ready (read or pre-configured).
// ============================================================================

tiny_hsm_result_t startup_state_device_id(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      // Initialize the device identity manager if not already done.
      if (!bridge->device_identity_manager_.is_complete() && !bridge->device_identity_manager_.is_failed()) {
        bridge->device_identity_manager_.init(
            bridge->configured_device_id_,
            bridge->autodiscovery_manager_.get_active_erd_client(),
            bridge->autodiscovery_manager_.get_host_address());
      }
      // Start the phase timeout timer.
      bridge->device_id_phase_start_ms_ = millis();
      // If a device_id is pre-configured, the manager is already complete
      // from init().  Sync the final_device_id_ and transition.
      if (bridge->device_identity_manager_.is_complete()) {
        bridge->notify_device_id_sensors_();
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      }
      break;

    case signal_run_loop:
      // Check for phase timeout — prevent indefinite stalls.
      if (millis() - bridge->device_id_phase_start_ms_ >= bridge->DEVICE_ID_PHASE_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Device ID phase timed out after %u ms, using fallback",
                 static_cast<unsigned>(bridge->DEVICE_ID_PHASE_TIMEOUT_MS));
        bridge->notify_device_id_sensors_();
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
        break;
      }

      bridge->device_identity_manager_.run();

      if (bridge->device_identity_manager_.is_complete()) {
        bridge->notify_device_id_sensors_();
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      } else if (bridge->device_identity_manager_.is_failed()) {
        // Even on failure, we have a fallback device ID — continue startup.
        ESP_LOGW(TAG, "Device ID generation failed, using fallback");
        bridge->notify_device_id_sensors_();
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      }
      break;

    case signal_device_id_complete:
      bridge->notify_device_id_sensors_();
      tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      break;

    case signal_device_id_failed:
      bridge->notify_device_id_sensors_();
      tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 4: MQTT Client Init — initialize the MQTT client adapter
//
// Initializes the MQTT client adapter with the device ID, then transitions
// to feature_bits.  This phase is fast — it doesn't wait for MQTT connection.
// ============================================================================

tiny_hsm_result_t startup_state_mqtt_client_init(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      if (!bridge->mqtt_client_adapter_initialized_) {
        bridge->initialize_mqtt_client_();
      }
      // Start feature bit reading in parallel — it can proceed while we wait
      // for MQTT to connect.
      bridge->start_feature_bit_reading_();
      tiny_hsm_transition(hsm, startup_state_feature_bits);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 5: Feature Bits — read appliance API feature bit ERDs
//
// Runs the FeatureBitManager each loop iteration.  Transitions to
// bridge_init when feature bits are complete AND MQTT is connected.
// ============================================================================

tiny_hsm_result_t startup_state_feature_bits(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Feature bits phase");
      bridge->feature_bits_phase_start_ms_ = millis();
      break;

    case signal_run_loop:
      {
      // Check for phase timeout — prevent indefinite stalls.
      if (millis() - bridge->feature_bits_phase_start_ms_ >= bridge->FEATURE_BITS_PHASE_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Feature bits phase timed out after %u ms, continuing without feature filtering",
                 static_cast<unsigned>(bridge->FEATURE_BITS_PHASE_TIMEOUT_MS));
        // Mark the manager as complete so is_complete() returns true and
        // we can transition to bridge_init.
        bridge->feature_bit_manager_.mark_timed_out();
      }

      bridge->feature_bit_manager_.run();

      // Check if we can transition to bridge_init:
      // Feature bits must be complete AND MQTT must be connected.
      bool feature_bits_done = bridge->feature_bit_manager_.is_complete();
      bool mqtt_connected = (mqtt::global_mqtt_client != nullptr &&
                             mqtt::global_mqtt_client->is_connected());

      if (feature_bits_done && mqtt_connected) {
        tiny_hsm_transition(hsm, startup_state_bridge_init);
      }
      }
      break;

    case signal_mqtt_connected:
      // MQTT just connected — check if feature bits are also done.
      if (bridge->feature_bit_manager_.is_complete()) {
        tiny_hsm_transition(hsm, startup_state_bridge_init);
      }
      break;

    case signal_feature_bits_complete:
      // Feature bits just completed — check if MQTT is connected.
      {
        bool mqtt_connected = (mqtt::global_mqtt_client != nullptr &&
                               mqtt::global_mqtt_client->is_connected());
        if (mqtt_connected) {
          tiny_hsm_transition(hsm, startup_state_bridge_init);
        }
      }
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 6: Bridge Init — initialize the MQTT bridge (poll or subscribe)
//
// Waits for MQTT connection, then initializes the appropriate bridge.
// Transitions to subscription_watch on completion.
// ============================================================================

tiny_hsm_result_t startup_state_bridge_init(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Bridge init phase");
      break;

    case signal_run_loop:
      if (!bridge->mqtt_bridge_initialized_ &&
          bridge->autodiscovery_manager_.is_complete() &&
          mqtt::global_mqtt_client != nullptr &&
          mqtt::global_mqtt_client->is_connected()) {
        ESP_LOGI(TAG, "Device ID ready and MQTT connected, initializing MQTT bridge");
        bridge->initialize_mqtt_bridge_();
        tiny_hsm_transition(hsm, startup_state_subscription_watch);
      }
      break;

    case signal_mqtt_connected:
      // MQTT just connected — try to initialize bridge now.
      if (!bridge->mqtt_bridge_initialized_ && bridge->autodiscovery_manager_.is_complete()) {
        ESP_LOGI(TAG, "MQTT connected, initializing MQTT bridge");
        bridge->initialize_mqtt_bridge_();
        tiny_hsm_transition(hsm, startup_state_subscription_watch);
      }
      break;

    case signal_bridge_ready:
      // Bridge was initialized externally — transition.
      tiny_hsm_transition(hsm, startup_state_subscription_watch);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 7: Subscription Watch — AUTO mode subscription watchdog
//
// In AUTO mode, monitors subscription activity and falls back to polling
// if no activity is detected within the timeout.  In poll/subscribe modes,
// this phase is a no-op and transitions immediately.
// ============================================================================

tiny_hsm_result_t startup_state_subscription_watch(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      // In non-AUTO modes, skip straight to HA discovery.
      if (bridge->mode_ != BRIDGE_MODE_AUTO) {
        bridge->maybe_start_custom_erd_polling_();
        tiny_hsm_transition(hsm, startup_state_ha_discovery);
      }
      break;

    case signal_run_loop:
      if (bridge->mode_ == BRIDGE_MODE_AUTO && bridge->subscription_mode_active_) {
        bridge->check_subscription_activity_();
      }
      bridge->maybe_start_custom_erd_polling_();
      bridge->log_poll_state_transitions_();

      // If subscription fallback happened (or we're not in AUTO mode),
      // transition to HA discovery.
      if (bridge->mode_ != BRIDGE_MODE_AUTO || !bridge->subscription_mode_active_) {
        tiny_hsm_transition(hsm, startup_state_ha_discovery);
      }
      break;

    case signal_subscription_fallback:
      // AUTO mode subscription timed out — transition to HA discovery.
      tiny_hsm_transition(hsm, startup_state_ha_discovery);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 8: HA Discovery — publish Home Assistant entity configs
//
// Runs the HaDiscoveryManager each loop iteration.  Transitions to
// heap_monitor once HA discovery is complete or not needed.
// ============================================================================

tiny_hsm_result_t startup_state_ha_discovery(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: HA discovery phase");
      break;

    case signal_run_loop:
      bridge->ha_discovery_manager_.run(
          !((bridge->mode_ == BRIDGE_MODE_SUBSCRIBE) ||
            (bridge->mode_ == BRIDGE_MODE_AUTO && bridge->subscription_mode_active_)),
          bridge->mqtt_bridge_polling_.polling_list_complete,
          bridge->subscription_activity_detected_,
          mqtt::global_mqtt_client);

      // Transition to running — HA discovery runs in the background
      // and doesn't block startup.
      tiny_hsm_transition(hsm, startup_state_running);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 9: Running — steady-state operation
//
// All recurring tasks run every loop() iteration.  This is the terminal
// state of the startup sequence.
// ============================================================================

tiny_hsm_result_t startup_state_running(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  GeappliancesBridge* bridge = bridge_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Bridge is now in steady-state operation");
      break;

    case signal_run_loop:
      // Steady-state: run all recurring tasks every loop iteration.
      // (run_protocol_stack_() is called above the HSM for all phases.)
      bridge->autodiscovery_manager_.run();
      bridge->device_identity_manager_.run();
      bridge->feature_bit_manager_.run();

      if (bridge->mode_ == BRIDGE_MODE_AUTO && bridge->subscription_mode_active_) {
        bridge->check_subscription_activity_();
      }
      bridge->maybe_start_custom_erd_polling_();
      bridge->log_poll_state_transitions_();

      bridge->ha_discovery_manager_.run(
          !((bridge->mode_ == BRIDGE_MODE_SUBSCRIBE) ||
            (bridge->mode_ == BRIDGE_MODE_AUTO && bridge->subscription_mode_active_)),
          bridge->mqtt_bridge_polling_.polling_list_complete,
          bridge->subscription_activity_detected_,
          mqtt::global_mqtt_client);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// HSM configuration — state descriptors with parent hierarchy
// ============================================================================

static const tiny_hsm_state_descriptor_t startup_hsm_state_descriptors[] = {
  { .state = startup_state_top,              .parent = nullptr },
  { .state = startup_state_protocol_stack,   .parent = startup_state_top },
  { .state = startup_state_autodiscovery,    .parent = startup_state_top },
  { .state = startup_state_device_id,        .parent = startup_state_top },
  { .state = startup_state_mqtt_client_init, .parent = startup_state_top },
  { .state = startup_state_feature_bits,     .parent = startup_state_top },
  { .state = startup_state_bridge_init,      .parent = startup_state_top },
  { .state = startup_state_subscription_watch, .parent = startup_state_top },
  { .state = startup_state_ha_discovery,     .parent = startup_state_top },
  { .state = startup_state_running,          .parent = startup_state_top },
};

const tiny_hsm_configuration_t startup_hsm_configuration = {
  .states = startup_hsm_state_descriptors,
  .state_count = element_count(startup_hsm_state_descriptors)
};

}  // namespace geappliances_bridge
}  // namespace esphome
