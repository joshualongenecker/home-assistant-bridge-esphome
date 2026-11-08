
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
      break;

    default:
      break;
  }

  return tiny_hsm_result_signal_consumed;
}

// Waits AUTODISCOVERY_STARTUP_DELAY_MS before transitioning to autodiscovery.
// This gives the appliance board time to boot and be ready to respond to
// broadcast requests.
tiny_hsm_result_t startup_state_startup_delay(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      svc->record_startup_delay_start();
      ESP_LOGI(TAG, "Startup: %u second stabilization delay",
               static_cast<unsigned>(AUTODISCOVERY_STARTUP_DELAY_MS / 1000));
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

// If no board responds, the manager keeps retrying indefinitely — this state
// will not transition until a valid board address is found.
tiny_hsm_result_t startup_state_autodiscovery(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Autodiscovery phase");
      svc->run_autodiscovery();
      break;

    case signal_run_loop:
      if (svc->is_autodiscovery_complete()) {
        ESP_LOGI(TAG, "Autodiscovery complete (host=0x%02X, protocol=%s)",
                 svc->get_discovered_host_address(),
                 svc->is_discovered_gea2_protocol() ? "GEA2" : "GEA3");
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
      if (svc->is_device_id_complete()) {
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      }
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// This phase is fast — it doesn't wait for MQTT connection.
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

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

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

        if (svc->check_steady_state()) {
          tiny_hsm_transition(hsm, startup_state_running);
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

      svc->check_steady_state();
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

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
