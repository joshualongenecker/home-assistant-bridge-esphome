/*!
 * @file
 * @brief Concurrency stress for the DiscoveryLifecycleManager claim protocol.
 *
 * Simulates the dual-core device pattern the phase machine was written for:
 * trigger_discovery_refresh()/trigger_ota_cleanup() arriving from a second
 * execution context (on the device, an MQTT-command-driven button press)
 * while the main loop drives loop(). The host cannot model portMUX spinlocks
 * or cross-core interrupts; it models the C++ memory model, which is exactly
 * what the atomic claim relies on.
 *
 * Two trigger threads hammer the claim from Idle while the main thread runs
 * the machine through full cleanup -> publish -> reboot cycles and samples
 * the pairing invariants after every iteration:
 *
 *   - a queued or active phase is never paired with kTriggerNone;
 *   - trigger_ never changes while a cycle is in flight (a losing trigger
 *     must not be able to write it, and a mid-cycle re-queue would
 *     necessarily rewrite trigger_). Sampling is once per loop() call, so a
 *     completed cycle that is immediately re-claimed between samples is
 *     legitimately seen as active -> queued; there is no phase-edge check.
 *
 * Under `make test TSAN=1` ThreadSanitizer additionally checks the
 * happens-before edges: the claimer's write to trigger_ must be published by
 * the release store on phase_, and the main thread must acquire phase_ before
 * reading trigger_. The pre-claim check-then-act implementation fails that
 * check deterministically; the invariant counters alone are probabilistic.
 *
 * The trigger threads only touch the atomic phase and the claim path; every
 * module effect (cleanup, publish, NVS hooks, reboot) stays on the main
 * thread, exactly as in production.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

extern "C" {
#include "erd_cache.h"
#include "ha_discovery_cleanup.h"
#include "ha_discovery_manager.h"
}

#include "discovery_lifecycle_manager.h"
#include "geappliances_bridge_constants.h"
#include "double/esphome_hal_double.hpp"
#include "double/mqtt_test_double.hpp"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "esphome_mqtt_client_adapter.h"

#include <atomic>
#include <cstring>
#include <thread>

using namespace esphome::geappliances_bridge;

namespace {

using Phase = DiscoveryLifecycleManager::Phase;
using Trigger = DiscoveryLifecycleManager::Trigger;

bool stub_is_ready(void*) { return true; }

void hook_noop(void*) {}
void hook_noop_manager(void*, ha_discovery_manager_t*) {}
bool hook_load_none(void*, DiscoveryLifecycleManager::DiscoveryRecord*) { return false; }
void hook_save(void*, const DiscoveryLifecycleManager::DiscoveryRecord&) {}

}  // namespace

TEST_GROUP(discovery_lifecycle_concurrency)
{
  ha_discovery_manager_t manager_;
  erd_cache_t cache_;
  esphome_mqtt_client_adapter_t adapter_;
  esphome::mqtt::MqttTestDouble mqtt_double_;
  tiny_gea3_erd_client_double_t erd_client_;
  DeviceIdentityManager identity_;
  DiscoveryLifecycleManager lifecycle_;

  void setup()
  {
    mock().clear();
    esphome_hal_double_set_millis(0);

    memset(&manager_, 0, sizeof(manager_));
    ha_discovery_manager_init(&manager_);
    erd_cache_init(&cache_);
    esphome_mqtt_client_adapter_init(&adapter_, "test_device");
    esphome::mqtt::global_mqtt_client = &mqtt_double_;
    mqtt_double_.connected_ = true;
    tiny_gea3_erd_client_double_init(&erd_client_);
    complete_device_identity();

    DiscoveryLifecycleManager::Hooks hooks{};
    hooks.apply_mqtt_availability = &hook_noop_manager;
    hooks.load_discovery_record = &hook_load_none;
    hooks.save_discovery_record = &hook_save;
    hooks.confirm_firmware = &hook_noop;
    hooks.feed_watchdog = &hook_noop;
    hooks.reboot = &hook_noop;
    lifecycle_.init(&manager_, identity_, adapter_, &cache_,
                    true, true, true, &stub_is_ready, nullptr, hooks);
  }

  void teardown()
  {
    ha_discovery_manager_cleanup(&manager_);
    erd_cache_destroy(&cache_);
    esphome_mqtt_client_adapter_destroy(&adapter_);
    esphome::mqtt::global_mqtt_client = nullptr;
    mock().clear();
  }

  void complete_device_identity()
  {
    expect_read(ERD_APPLIANCE_TYPE);
    identity_.init("", &erd_client_.interface, 0xC0);

    const uint8_t appliance_type = 0x06;
    expect_read(ERD_MODEL_NUMBER);
    identity_.on_erd_read_completed(ERD_APPLIANCE_TYPE, &appliance_type, 1);

    expect_read(ERD_SERIAL_NUMBER);
    identity_.on_erd_read_completed(ERD_MODEL_NUMBER,
                                    reinterpret_cast<const uint8_t*>("GTCH2230"), 8);

    identity_.on_erd_read_completed(ERD_SERIAL_NUMBER,
                                    reinterpret_cast<const uint8_t*>("SN123456"), 8);
    mock().checkExpectations();
  }

  void expect_read(tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client_.interface)
      .withParameter("address", 0xC0)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }
};

TEST(discovery_lifecycle_concurrency, concurrent_triggers_never_corrupt_the_phase_machine)
{
  /* Enough cycles to give the scheduler many chances to land a losing
   * trigger write inside an active cycle. */
  static const unsigned int kTargetCycles = 100;
  static const int kMaxIterations = 200000;

  std::atomic<bool> stop{false};
  std::atomic<unsigned int> refresh_calls{0};
  std::atomic<unsigned int> ota_calls{0};

  std::thread refresh([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      lifecycle_.trigger_discovery_refresh();
      refresh_calls.fetch_add(1, std::memory_order_relaxed);
    }
  });
  std::thread ota([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      lifecycle_.trigger_ota_cleanup();
      ota_calls.fetch_add(1, std::memory_order_relaxed);
    }
  });

  Trigger cycle_trigger = DiscoveryLifecycleManager::kTriggerNone;
  bool in_cycle = false;
  unsigned int cycles = 0;
  unsigned int unpaired_phase = 0;
  unsigned int mid_cycle_trigger_change = 0;
  Phase previous = lifecycle_.phase();

  int i = 0;
  for (; i < kMaxIterations && cycles < kTargetCycles; i++) {
    /* Step the fake clock so cleanup converges and the reboot delay
     * elapses; the hal-double clock is only ever touched from here. */
    esphome_hal_double_set_millis(esphome_hal_double_get_millis() + 600);

    /* A full publish run needs appliance discovery payloads the cache
     * does not have; steer the run to completion like the single-threaded
     * tests do. */
    if (lifecycle_.phase() == DiscoveryLifecycleManager::kPhasePublishing) {
      manager_.state = ha_discovery_state_complete;
    }

    lifecycle_.loop();
    const Phase now = lifecycle_.phase();

    /* trigger_ is only read when phase_ shows a queued or active phase:
     * the acquire load of phase_ then orders every trigger_ write the
     * claim protocol made visible. Reading it alongside Idle or the
     * transient Claimed would race the claimer by design. The trigger is
     * "live" from the claim until finish_publish_ clears it on the way to
     * RebootPending; from RebootPending on, kTriggerNone is correct. */
    const bool is_queued = now == DiscoveryLifecycleManager::kPhaseQueuedInitial ||
                           now == DiscoveryLifecycleManager::kPhaseQueuedCleanup;
    const bool is_active_pre_completion =
        now == DiscoveryLifecycleManager::kPhaseCleaning ||
        now == DiscoveryLifecycleManager::kPhasePublishing;
    if (is_queued || is_active_pre_completion) {
      const Trigger trig = lifecycle_.trigger();
      if (is_queued && trig == DiscoveryLifecycleManager::kTriggerNone) unpaired_phase++;
      if (!in_cycle) {
        in_cycle = true;
        cycle_trigger = trig;
      } else if (cycle_trigger != trig) {
        mid_cycle_trigger_change++;
      }
    } else {
      if (previous == DiscoveryLifecycleManager::kPhaseRebootPending &&
          now == DiscoveryLifecycleManager::kPhaseIdle) {
        cycles++;
      }
      in_cycle = false;
    }
    previous = now;
  }

  stop.store(true);
  refresh.join();
  ota.join();

  /* The harness must actually have exercised the machine, or the invariant
   * counters above are vacuous. */
  CHECK_TRUE(cycles >= kTargetCycles);
  CHECK_TRUE(refresh_calls.load() > 0);
  CHECK_TRUE(ota_calls.load() > 0);
  CHECK_EQUAL(0u, unpaired_phase);
  CHECK_EQUAL(0u, mid_cycle_trigger_change);
}
