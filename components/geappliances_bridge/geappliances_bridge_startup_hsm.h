#ifndef startup_hsm_h
#define startup_hsm_h

#include "i_bridge_services.h"

extern "C" {
#include "tiny_hsm.h"
}

// Startup sequencing is driven by polling: loop() sends signal_run_loop on every
// iteration, and each state's run_loop handler polls the IBridgeServices
// completion predicate that owns its transition.  There is deliberately no
// per-phase completion signal — a duplicate event path would let the same
// transition be triggered two ways.
//
// signal_bridge_ready is the single exception: the bridge_init ->
// subscription_watch transition fires when ERD discovery finishes, and no
// IBridgeServices predicate describes "discovery finished" (is_bridge_initialized()
// goes true before the probe phase completes in polling mode), so that one
// transition stays event-driven.
enum {
  signal_run_loop = tiny_hsm_signal_user_start,
  signal_bridge_ready
};

namespace esphome {
namespace geappliances_bridge {
typedef struct {
  tiny_hsm_t hsm;
  IBridgeServices* services;
} startup_hsm_wrapper_t;

tiny_hsm_result_t startup_state_top(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_startup_delay(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_autodiscovery(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_device_id(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_mqtt_client_init(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_feature_bits(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_bridge_init(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_subscription_watch(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_running(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
IBridgeServices* services_from_hsm(tiny_hsm_t* hsm);

void startup_hsm_wrapper_init(startup_hsm_wrapper_t* self, IBridgeServices* services,
  tiny_hsm_state_t initial);

void startup_hsm_wrapper_destroy(startup_hsm_wrapper_t* self);

extern const tiny_hsm_configuration_t startup_hsm_configuration;

}
}

#endif
