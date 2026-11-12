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
 * Two trigger threads hammer the claim while the main thread runs the machine
 * through full cleanup -> publish -> reboot cycles and samples the pairing
 * invariants after every iteration:
 *
 *   - a queued or active phase is never paired with kTriggerNone;
 *   - trigger_ never changes while a cycle is in flight (a losing trigger
 *     must not be able to write it, and a mid-cycle re-queue would
 *     necessarily rewrite trigger_).
 *
 * The main thread grants the next Idle window to one trigger kind at a time
 * (`wanted` below) and opens the gate to both kinds while a cycle is in
 * flight.  The claim is a bare compare-exchange with no fairness mechanism,
 * and none is wanted, but an un-gated stress run is not a fair sample of it:
 * on a two-core host the scheduler parks one thread for whole stretches, so
 * the same kind can win every one of the 100 claims and the "both kinds
 * completed a cycle" check then fails on a healthy machine.  Only the Idle
 * window is single-kind; the other kind keeps calling its trigger for the
 * rest of the cycle, which is what puts a losing writer in front of the
 * mid-cycle check.
 *
 * Sampling is once per loop() call. A completed cycle always shows at least
 * one RebootPending sample (the delay cannot elapse in the call that enters
 * the phase), which clears the in-cycle flag, so a fresh claim after the
 * reboot is never mistaken for a mid-cycle change even when the Idle sample
 * is skipped by a fast claimer. The one window that could false-positive is
 * an abort that leaves a phase inside a single loop() call (the
 * kPhaseTimeoutMs aborts, or a FAILED run) being immediately re-claimed
 * before the next sample; neither abort is reachable in this test (the
 * stepped clock never comes near the 5-minute budget, and publish runs are
 * steered to COMPLETE), so the counter stays exact here.
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

  /* Which thread may claim the next Idle window.  The main thread opens the
   * gate to both kinds - kTriggerNone, a value neither thread ever claims -
   * while a cycle is in flight, and closes it on the first sample outside a
   * cycle, which hands the next Idle window to the kind that did not just
   * run one.  One atomic carries both jobs on purpose: a separate "gate
   * open" flag would need two stores, and a thread could read the gate
   * closed before reading the new grant, leaving a window no kind owns.
   *
   * Without the gate the both-kinds assertion below measures the host
   * scheduler instead of the claim protocol: with two spinning threads on a
   * two-core host one of them stays off-CPU long enough to lose all 100
   * claims (measured 13 failures in 150 full-suite runs under
   * `taskset -c 0,1`, and either kind could be the starved one).  Gating the
   * Idle windows alone would trade that for the mid-cycle check, whose only
   * signal is trigger_ differing from cycle_trigger: if only the winner ever
   * calls a trigger, a regression that lets a losing trigger write trigger_
   * stores the value the winner already stored and the check cannot see it.
   * So the loser keeps hammering the reject path for the whole cycle.  The
   * race stays live in both directions: the gate is read before the call, so
   * a thread that passes it and is then preempted still lands its claim
   * attempt on a phase the other thread already moved. */
  std::atomic<Trigger> wanted{DiscoveryLifecycleManager::kTriggerDiscoveryRefresh};

  std::thread refresh([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      const Trigger w = wanted.load(std::memory_order_relaxed);
      if (w == DiscoveryLifecycleManager::kTriggerDiscoveryRefresh ||
          w == DiscoveryLifecycleManager::kTriggerNone) {
        lifecycle_.trigger_discovery_refresh();
      }
      std::this_thread::yield();
      refresh_calls.fetch_add(1, std::memory_order_relaxed);
    }
  });
  std::thread ota([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      const Trigger w = wanted.load(std::memory_order_relaxed);
      if (w == DiscoveryLifecycleManager::kTriggerOta ||
          w == DiscoveryLifecycleManager::kTriggerNone) {
        lifecycle_.trigger_ota_cleanup();
      }
      std::this_thread::yield();
      ota_calls.fetch_add(1, std::memory_order_relaxed);
    }
  });

  Trigger cycle_trigger = DiscoveryLifecycleManager::kTriggerNone;
  bool in_cycle = false;
  unsigned int cycles = 0;
  unsigned int unpaired_phase = 0;
  unsigned int refresh_cycles = 0;
  unsigned int ota_cycles = 0;
  unsigned int mid_cycle_trigger_change = 0;
  /* Idle iterations to tolerate before treating the waiting kind as stuck.
   * A healthy claim lands within a couple of iterations of Idle, so this only
   * fires when the enabled thread cannot claim at all. */
  static const unsigned int kIdleStallSpins = 500;
  unsigned int idle_spins = 0;

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
        /* Only the Idle window is granted to one kind at a time.  Once a
         * cycle is in flight the gate opens to both, so the kind that lost
         * the claim keeps trying to write trigger_ for the whole cycle;
         * that is the race the mid-cycle check exists to see. */
        wanted.store(DiscoveryLifecycleManager::kTriggerNone, std::memory_order_relaxed);
      } else if (cycle_trigger != trig) {
        mid_cycle_trigger_change++;
      }
    } else {
      /* Outside a cycle the gate closes again, handing the next Idle window
       * to the kind that did not just run one.  Keying on the open gate
       * rather than on the phase is what makes this the backstop too: a
       * cycle that ends without a RebootPending sample - a kPhaseTimeoutMs
       * abort, a FAILED publish run - still gets its next Idle window
       * granted to one kind instead of to both. */
      if (wanted.load(std::memory_order_relaxed) ==
          DiscoveryLifecycleManager::kTriggerNone) {
        wanted.store(cycle_trigger == DiscoveryLifecycleManager::kTriggerDiscoveryRefresh
                        ? DiscoveryLifecycleManager::kTriggerOta
                        : DiscoveryLifecycleManager::kTriggerDiscoveryRefresh,
                     std::memory_order_relaxed);
      }
      if (previous == DiscoveryLifecycleManager::kPhaseRebootPending &&
          now == DiscoveryLifecycleManager::kPhaseIdle) {
        cycles++;
        /* Attempts are not enough: a regression that drops, say, OTA claims
         * from Idle must fail here, so both trigger kinds must actually win
         * full cycles. */
        if (cycle_trigger == DiscoveryLifecycleManager::kTriggerDiscoveryRefresh) {
          refresh_cycles++;
        } else if (cycle_trigger == DiscoveryLifecycleManager::kTriggerOta) {
          ota_cycles++;
        }
      }
      in_cycle = false;
    }
    if (now == DiscoveryLifecycleManager::kPhaseIdle) {
      /* Idle means the machine is waiting on the thread holding permission,
       * and spinning here without yielding is what keeps that thread off the
       * CPU on a host with fewer cores than runnable threads.  If it never
       * claims at all - a regression that drops one kind's claim, or a thread
       * this host never schedules - hand permission to the other kind rather
       * than burning the iteration budget, so the run finishes and the
       * both-kinds assertion names the kind that is missing instead of
       * reporting a stalled loop. */
      if (++idle_spins > kIdleStallSpins) {
        idle_spins = 0;
        wanted.store(wanted.load(std::memory_order_relaxed) ==
                             DiscoveryLifecycleManager::kTriggerDiscoveryRefresh
                         ? DiscoveryLifecycleManager::kTriggerOta
                         : DiscoveryLifecycleManager::kTriggerDiscoveryRefresh,
                     std::memory_order_relaxed);
      }
      std::this_thread::yield();
    } else {
      idle_spins = 0;
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
  CHECK_TRUE(refresh_cycles > 0);
  CHECK_TRUE(ota_cycles > 0);
  CHECK_EQUAL(0u, unpaired_phase);
  CHECK_EQUAL(0u, mid_cycle_trigger_change);
}
