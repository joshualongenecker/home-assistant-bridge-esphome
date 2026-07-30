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

#include "i_bridge_services.h"
#include "erd_bridge_common.h"
#include "geappliances_bridge_constants.h"
#include "geappliances_bridge_startup_hsm.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

extern "C" {
#include "tiny_utils.h"  // element_count macro
}

GEA_TAG(TAG) = "geappliances_bridge_startup_hsm";

namespace esphome {
namespace geappliances_bridge {



IBridgeServices* services_from_hsm(tiny_hsm_t* hsm)
{
  if (!hsm) return nullptr;
  startup_hsm_wrapper_t* wrapper = container_of(startup_hsm_wrapper_t, hsm, hsm);
  return wrapper->services;
}

void startup_hsm_wrapper_init(startup_hsm_wrapper_t* self, IBridgeServices* services,
  tiny_hsm_state_t initial)
{
  self->services = services;
  tiny_hsm_init(&self->hsm, &startup_hsm_configuration, initial);
}

void startup_hsm_wrapper_destroy(startup_hsm_wrapper_t* self)
{
  self->services = nullptr;
  self->hsm.current = nullptr;
}


// ============================================================================
// Top state — handles signals common to all startup phases
// ============================================================================

tiny_hsm_result_t startup_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)data;
  (void)hsm;

  // The top state is the root of the hierarchy.  Any signal not consumed
  // by a child state will bubble up here.  We consume it to avoid the
  // signal being silently dropped (deferred at root = lost).
  switch (signal) {
    case tiny_hsm_signal_entry:
    case tiny_hsm_signal_exit:
      // No common entry/exit logic needed.
      break;

    default:
      // Unhandled signal at root — consume it to avoid silent loss.
      break;
  }

  return tiny_hsm_result_signal_consumed;
}

// Phase 1: Protocol Stack — drive GEA2/GEA3 hardware
//
// This is the initial state.  It transitions to autodiscovery as soon as
// the first loop() call arrives (the protocol stack is always running).
// ============================================================================
// ============================================================================
// Phase 1.5: Startup Delay — wait for appliance board to stabilize
//
// Waits for the configured startup delay before transitioning to
// autodiscovery. This gives the appliance board time to boot and be ready
// to respond to broadcast requests.
// ============================================================================

tiny_hsm_result_t startup_state_startup_delay(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      svc->record_startup_delay_start();
      ESP_LOGI(TAG, "Startup: stabilization delay");
      break;

    case signal_run_loop:
      if (svc->is_startup_delay_elapsed()) {
        tiny_hsm_transition(hsm, startup_state_autodiscovery);
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
// Phase 2: Autodiscovery — find appliance on bus
//
// The AutodiscoveryManager is fully self-driving: it owns its own timers
// and event subscriptions.  The HSM calls start() on entry, then checks
// for completion on each loop iteration and via the completion signal.
// If no board responds, the manager keeps retrying indefinitely — this
// state will not transition until a valid board address is found.
// ============================================================================

tiny_hsm_result_t startup_state_autodiscovery(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Autodiscovery phase");
      // Kick off the self-driving autodiscovery manager.
      svc->run_autodiscovery();
      break;

    case signal_run_loop:
      // Manager is self-driving — just check if it completed.
      if (svc->is_autodiscovery_complete()) {
        ESP_LOGI(TAG, "Autodiscovery complete (host=0x%02X, protocol=%s)",
                 svc->get_discovered_host_address(),
                 svc->is_discovered_gea2_protocol() ? "GEA2" : "GEA3");
        tiny_hsm_transition(hsm, startup_state_device_id);
      }
      break;

    case signal_autodiscovery_complete:
      // External signal from autodiscovery callback — transition only if
      // a board was actually discovered (not on failure/no-response).
      if (svc->is_autodiscovery_complete()) {
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
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      svc->init_device_id_reading();
      // If a device_id is pre-configured and the manager completes synchronously
      // during init(), transition immediately.
      if (svc->is_device_id_complete()) {
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      }
      break;

    case signal_run_loop:
      // Manager is fully self-driving — just check if it completed.
      if (svc->is_device_id_complete()) {
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      }
      break;

    case signal_device_id_complete:
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
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      if (!svc->is_mqtt_client_initialized()) {
        svc->initialize_mqtt_client();
      }
      if (!svc->is_erd_cache_publisher_initialized()) {
        svc->initialize_erd_cache_publisher();
      }
      svc->start_feature_bit_reading();
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
// FeatureBitManager is self-driving (owns its own timers and event subscriptions).
// The HSM only polls is_feature_bits_complete() to know when to transition.
// Transitions to bridge_init when feature bits are complete AND MQTT is connected.
// ============================================================================

tiny_hsm_result_t startup_state_feature_bits(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Feature bits phase");
      break;

    case signal_run_loop:
      {
      bool feature_bits_done = svc->is_feature_bits_complete();
      if (feature_bits_done) {
        tiny_hsm_transition(hsm, startup_state_bridge_init);
      }
      }
      break;

    case signal_mqtt_connected:
      if (svc->is_feature_bits_complete()) {
        tiny_hsm_transition(hsm, startup_state_bridge_init);
      }
      break;

    case signal_feature_bits_complete:
      {
        tiny_hsm_transition(hsm, startup_state_bridge_init);
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
// Phase 6: Bridge Init — initialize the ERD bridge (poll or subscribe)
//
// Waits for MQTT connection, then initializes the appropriate bridge.
// Transitions to subscription_watch on completion.
// ============================================================================

tiny_hsm_result_t startup_state_bridge_init(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGD(TAG, "Startup: Bridge init phase");
      break;

    case signal_run_loop:
      if (!svc->is_bridge_initialized() &&
          svc->is_autodiscovery_complete()) {
        svc->initialize_erd_bridge();
        // Do NOT transition here — wait for signal_bridge_ready from the
        // polling bridge when ERD discovery is complete.
      }
      break;

    case signal_bridge_ready:
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
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      if (svc->get_mode() != BRIDGE_MODE_AUTO) {
        svc->maybe_start_custom_erd_polling();
        tiny_hsm_transition(hsm, startup_state_running);
      }
      break;

    case signal_run_loop:
      {
        subscription_state_t sub_state = svc->get_subscription_state();
        if (sub_state == subscription_state_failed) {
          svc->handle_subscription_failed();
          tiny_hsm_transition(hsm, startup_state_running);
          break;
        }
        svc->log_poll_state_transitions();
        svc->handle_polling_failed();
        svc->maybe_start_custom_erd_polling();

        // Check if the appliance bridge has reached steady state.
        if (svc->check_steady_state()) {
          tiny_hsm_transition(hsm, startup_state_running);
        }
      }
      break;

    case signal_subscription_fallback:
      tiny_hsm_transition(hsm, startup_state_running);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}


// Phase 8: Running — steady-state operation
//
// All recurring tasks run every loop() iteration.  This is the terminal
// state of the startup sequence.
// ============================================================================

tiny_hsm_result_t startup_state_running(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      // Check steady state immediately on entry so the log fires even if
      // subsequent loop() calls are delayed by the long probe phase that
      // triggered the transition.
      svc->check_steady_state();
      break;

    case signal_run_loop:
      {
        subscription_state_t sub_state = svc->get_subscription_state();
        if (sub_state == subscription_state_failed) {
          svc->handle_subscription_failed();
        }
      }
      svc->handle_polling_failed();
      svc->log_poll_state_transitions();
      svc->maybe_start_custom_erd_polling();

      // Check and log once when the appliance bridge first reaches steady state.
      svc->check_steady_state();
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
  { .state = startup_state_startup_delay,    .parent = startup_state_top },
  { .state = startup_state_autodiscovery,    .parent = startup_state_top },
  { .state = startup_state_device_id,        .parent = startup_state_top },
  { .state = startup_state_mqtt_client_init, .parent = startup_state_top },
  { .state = startup_state_feature_bits,     .parent = startup_state_top },
  { .state = startup_state_bridge_init,      .parent = startup_state_top },
  { .state = startup_state_subscription_watch, .parent = startup_state_top },
  { .state = startup_state_running,          .parent = startup_state_top }
};

const tiny_hsm_configuration_t startup_hsm_configuration = {
  .states = startup_hsm_state_descriptors,
  .state_count = element_count(startup_hsm_state_descriptors)
};

}  // namespace geappliances_bridge
}  // namespace esphome
