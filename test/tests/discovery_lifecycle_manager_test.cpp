/*!
 * @file
 * @brief Unit tests for the DiscoveryLifecycleManager phase machine.
 *
 * These drive the real ha_discovery_manager_t and ha_discovery_cleanup_t; only
 * the host-environment effects (NVS, OTA-rollback confirmation, watchdog,
 * reboot, MQTT availability) are replaced by recording hooks. Every transition
 * below therefore runs the same code the device runs, which was not true
 * before: the whole of loop() used to be compiled out under USE_ESP_IDF_STUBS.
 *
 * Completion of a discovery run is steered by setting the manager's own state
 * field, because a full run needs the appliance discovery payload; the
 * discovery manager's own state machine is covered by
 * ha_discovery_manager_test.cpp.
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

#include <cstring>

using namespace esphome::geappliances_bridge;

namespace {

using Record = DiscoveryLifecycleManager::DiscoveryRecord;
using Phase = DiscoveryLifecycleManager::Phase;
using Trigger = DiscoveryLifecycleManager::Trigger;

/* Bridge readiness as seen through the injected predicate. */
bool bridge_ready = true;
bool stub_is_ready(void*) { return bridge_ready; }

/* Records every host-environment effect the phase machine asks for. */
struct HookRecorder {
  Record stored{};
  bool has_stored{false};
  Record saved{};
  int load_calls{0};
  int save_calls{0};
  int confirm_calls{0};
  int watchdog_calls{0};
  int reboot_calls{0};
  int availability_calls{0};

  void reset() { *this = HookRecorder{}; }
};

bool hook_load(void* context, Record* out) {
  auto* recorder = static_cast<HookRecorder*>(context);
  recorder->load_calls++;
  if (!recorder->has_stored) return false;
  *out = recorder->stored;
  return true;
}

void hook_save(void* context, const Record& record) {
  auto* recorder = static_cast<HookRecorder*>(context);
  recorder->save_calls++;
  recorder->saved = record;
}

void hook_confirm(void* context) {
  static_cast<HookRecorder*>(context)->confirm_calls++;
}

void hook_watchdog(void* context) {
  static_cast<HookRecorder*>(context)->watchdog_calls++;
}

void hook_reboot(void* context) {
  static_cast<HookRecorder*>(context)->reboot_calls++;
}

void hook_availability(void* context, ha_discovery_manager_t*) {
  static_cast<HookRecorder*>(context)->availability_calls++;
}

}  // namespace

TEST_GROUP(discovery_lifecycle_manager)
{
  ha_discovery_manager_t manager_;
  erd_cache_t cache_;
  esphome_mqtt_client_adapter_t adapter_;
  esphome::mqtt::MqttTestDouble mqtt_double_;
  tiny_gea3_erd_client_double_t erd_client_;
  DeviceIdentityManager identity_;
  HookRecorder hooks_;
  DiscoveryLifecycleManager lifecycle_;

  void setup()
  {
    mock().clear();
    esphome_hal_double_set_millis(0);
    hooks_.reset();
    bridge_ready = true;

    memset(&manager_, 0, sizeof(manager_));
    ha_discovery_manager_init(&manager_);
    erd_cache_init(&cache_);
    esphome_mqtt_client_adapter_init(&adapter_, "test_device");
    esphome::mqtt::global_mqtt_client = &mqtt_double_;
    mqtt_double_.connected_ = true;
    tiny_gea3_erd_client_double_init(&erd_client_);
    complete_device_identity();
    init_lifecycle();
  }

  void teardown()
  {
    ha_discovery_manager_cleanup(&manager_);
    erd_cache_destroy(&cache_);
    esphome_mqtt_client_adapter_destroy(&adapter_);
    esphome::mqtt::global_mqtt_client = nullptr;
    mock().clear();
  }

  /* Reads the three identity ERDs so is_ready() can report a complete
   * device identity: appliance type -> model number -> serial number. */
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

  /* Both discovery-affecting config flags are on; tests that need other
   * values re-init with them. */
  void init_lifecycle(bool generate_device_config = true,
                      DeviceIdentityManager* identity = nullptr)
  {
    DiscoveryLifecycleManager::Hooks hooks{};
    hooks.apply_mqtt_availability = &hook_availability;
    hooks.load_discovery_record = &hook_load;
    hooks.save_discovery_record = &hook_save;
    hooks.confirm_firmware = &hook_confirm;
    hooks.feed_watchdog = &hook_watchdog;
    hooks.reboot = &hook_reboot;
    hooks.context = &hooks_;
    lifecycle_.init(&manager_, identity != nullptr ? *identity : identity_, adapter_, &cache_,
                    generate_device_config, true, true,
                    &stub_is_ready, nullptr, hooks);
  }

  /* A stored record that matches the current inputs, i.e. "nothing to do". */
  Record matching_record() const
  {
    Record record{};
    record.version = DiscoveryLifecycleManager::kRecordVersion;
    record.hash = ha_discovery_manager_data_hash(&manager_);
    strncpy(record.device_id, identity_.get_device_id(), sizeof(record.device_id) - 1);
    record.appliance_api_parsing = true;
    record.filter_config_topics = true;
    return record;
  }

  void enter_publishing()
  {
    lifecycle_.trigger_initial_discovery();
    lifecycle_.loop();
    CHECK_EQUAL(DiscoveryLifecycleManager::kPhasePublishing, lifecycle_.phase());
  }

  void enter_cleaning()
  {
    lifecycle_.trigger_ota_cleanup();
    lifecycle_.loop();
    CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseCleaning, lifecycle_.phase());
  }

  /* Ends the in-flight discovery run with the given outcome. */
  void end_publish_run(ha_discovery_state_t outcome)
  {
    manager_.state = outcome;
    lifecycle_.loop();
  }
};

/* ------------------------------------------------------------------ */
/* initial state                                                       */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, starts_idle_with_nothing_queued)
{
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerNone, lifecycle_.trigger());
  CHECK(!lifecycle_.initial_publish_done());
  CHECK(lifecycle_.is_ready());
}

TEST(discovery_lifecycle_manager, is_ready_requires_bridge_and_device_identity)
{
  bridge_ready = false;
  CHECK(!lifecycle_.is_ready());
  bridge_ready = true;
  CHECK(lifecycle_.is_ready());
}

TEST(discovery_lifecycle_manager, is_ready_requires_complete_device_identity)
{
  /* A fresh identity manager that has only started reading the identity ERDs:
   * bridge readiness alone must not report ready. */
  /* Local on purpose: init_lifecycle() re-points lifecycle_ at identity_ in
   * the next test's setup, so the stale reference this test leaves behind is
   * never used. */
  DeviceIdentityManager pending_identity;
  expect_read(ERD_APPLIANCE_TYPE);
  pending_identity.init("", &erd_client_.interface, 0xC0);
  mock().checkExpectations();
  CHECK_TRUE(pending_identity.get_state() != DEVICE_ID_STATE_COMPLETE);

  init_lifecycle(true, &pending_identity);

  CHECK(!lifecycle_.is_ready());
}

TEST(discovery_lifecycle_manager, every_phase_has_a_distinct_name)
{
  const Phase phases[] = {
    DiscoveryLifecycleManager::kPhaseIdle,
    DiscoveryLifecycleManager::kPhaseQueuedInitial,
    DiscoveryLifecycleManager::kPhaseQueuedCleanup,
    DiscoveryLifecycleManager::kPhaseCleaning,
    DiscoveryLifecycleManager::kPhasePublishing,
    DiscoveryLifecycleManager::kPhaseRebootPending,
    DiscoveryLifecycleManager::kPhaseClaimed,
  };
  for (size_t i = 0; i < sizeof(phases) / sizeof(phases[0]); i++) {
    const char* name = DiscoveryLifecycleManager::phase_name(phases[i]);
    CHECK_TRUE(name != nullptr);
    CHECK_TRUE(name[0] != '\0');
    for (size_t j = i + 1; j < sizeof(phases) / sizeof(phases[0]); j++) {
      CHECK_TRUE(strcmp(name, DiscoveryLifecycleManager::phase_name(phases[j])) != 0);
    }
  }
}

/* ------------------------------------------------------------------ */
/* check_discovery_changes(): which action the stored record implies   */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, missing_record_queues_initial_publish)
{
  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(1, hooks_.availability_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedInitial, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerInitial, lifecycle_.trigger());
}

TEST(discovery_lifecycle_manager, matching_record_queues_nothing)
{
  hooks_.stored = matching_record();
  hooks_.has_stored = true;

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, stale_record_version_queues_cleanup)
{
  hooks_.stored = matching_record();
  hooks_.stored.version = DiscoveryLifecycleManager::kRecordVersion - 1;
  hooks_.has_stored = true;

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedCleanup, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerOta, lifecycle_.trigger());
}

TEST(discovery_lifecycle_manager, changed_hash_queues_cleanup)
{
  hooks_.stored = matching_record();
  hooks_.stored.hash += 1;
  hooks_.has_stored = true;

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedCleanup, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerOta, lifecycle_.trigger());
}

TEST(discovery_lifecycle_manager, changed_device_id_queues_cleanup)
{
  hooks_.stored = matching_record();
  strcpy(hooks_.stored.device_id, "old-range-gea");
  hooks_.has_stored = true;

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedCleanup, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, changed_config_flags_queue_cleanup)
{
  hooks_.stored = matching_record();
  hooks_.stored.filter_config_topics = false;
  hooks_.has_stored = true;

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedCleanup, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, empty_stored_device_id_does_not_count_as_change)
{
  hooks_.stored = matching_record();
  hooks_.stored.device_id[0] = '\0';
  hooks_.has_stored = true;

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, change_check_needs_generate_device_config)
{
  init_lifecycle(false);

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(0, hooks_.load_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, change_check_ignores_null_device_id)
{
  lifecycle_.check_discovery_changes(nullptr);

  CHECK_EQUAL(0, hooks_.load_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, change_check_is_skipped_while_discovery_runs)
{
  lifecycle_.trigger_initial_discovery();
  hooks_.stored = matching_record();
  hooks_.stored.hash += 1;
  hooks_.has_stored = true;

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(0, hooks_.load_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedInitial, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerInitial, lifecycle_.trigger());
}

/* ------------------------------------------------------------------ */
/* queued phases wait for readiness                                    */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, queued_initial_publish_waits_for_readiness)
{
  bridge_ready = false;
  lifecycle_.trigger_initial_discovery();

  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedInitial, lifecycle_.phase());
  CHECK_EQUAL(ha_discovery_state_idle, manager_.state);

  bridge_ready = true;
  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhasePublishing, lifecycle_.phase());
  /* The real manager was configured and started, not just the phase moved. */
  CHECK_EQUAL(ha_discovery_state_building, manager_.state);
}

TEST(discovery_lifecycle_manager, queued_initial_publish_needs_generate_device_config)
{
  init_lifecycle(false);
  lifecycle_.trigger_initial_discovery();

  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedInitial, lifecycle_.phase());
  CHECK_EQUAL(ha_discovery_state_idle, manager_.state);
}

TEST(discovery_lifecycle_manager, queued_cleanup_waits_for_readiness)
{
  bridge_ready = false;
  lifecycle_.trigger_discovery_refresh();

  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedCleanup, lifecycle_.phase());
  CHECK_EQUAL(ha_cleanup_state_idle, manager_.cleanup.state);

  bridge_ready = true;
  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseCleaning, lifecycle_.phase());
  /* The real cleanup module was started. */
  CHECK_EQUAL(ha_cleanup_state_cleaning, manager_.cleanup.state);
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerDiscoveryRefresh, lifecycle_.trigger());
}

TEST(discovery_lifecycle_manager, loop_in_idle_does_nothing)
{
  lifecycle_.loop();
  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
  CHECK_EQUAL(0, hooks_.save_calls);
  CHECK_EQUAL(0, hooks_.reboot_calls);
  CHECK_EQUAL(0, hooks_.watchdog_calls);
}

/* ------------------------------------------------------------------ */
/* one phase at a time                                                 */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, requests_are_rejected_while_a_phase_runs)
{
  lifecycle_.trigger_initial_discovery();

  lifecycle_.trigger_discovery_refresh();
  lifecycle_.trigger_ota_cleanup();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedInitial, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerInitial, lifecycle_.trigger());
}

TEST(discovery_lifecycle_manager, requests_are_rejected_while_cleaning)
{
  enter_cleaning();
  manager_.cleanup.state = ha_cleanup_state_cleaning;

  lifecycle_.trigger_discovery_refresh();
  lifecycle_.trigger_ota_cleanup();
  lifecycle_.trigger_initial_discovery();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseCleaning, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerOta, lifecycle_.trigger());
}

TEST(discovery_lifecycle_manager, requests_are_rejected_while_publishing_or_reboot_pending)
{
  enter_publishing();

  lifecycle_.trigger_discovery_refresh();
  lifecycle_.trigger_ota_cleanup();
  lifecycle_.trigger_initial_discovery();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhasePublishing, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerInitial, lifecycle_.trigger());

  end_publish_run(ha_discovery_state_complete);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseRebootPending, lifecycle_.phase());

  lifecycle_.trigger_discovery_refresh();
  lifecycle_.trigger_ota_cleanup();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseRebootPending, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerNone, lifecycle_.trigger());
}

TEST(discovery_lifecycle_manager, refresh_and_ota_cleanup_share_the_cleanup_phase)
{
  lifecycle_.trigger_discovery_refresh();
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerDiscoveryRefresh, lifecycle_.trigger());

  init_lifecycle();
  lifecycle_.trigger_ota_cleanup();
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerOta, lifecycle_.trigger());
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedCleanup, lifecycle_.phase());
}

/* ------------------------------------------------------------------ */
/* cleaning -> publishing                                              */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, cleaning_runs_until_the_cleanup_reports_done)
{
  enter_cleaning();

  /* Not done yet: the phase machine keeps driving the cleanup module. */
  manager_.cleanup.state = ha_cleanup_state_cleaning;
  lifecycle_.loop();
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseCleaning, lifecycle_.phase());

  manager_.cleanup.state = ha_cleanup_state_done;
  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhasePublishing, lifecycle_.phase());
  CHECK_EQUAL(ha_discovery_state_building, manager_.state);
}

TEST(discovery_lifecycle_manager, cleaning_drives_the_real_cleanup_to_completion)
{
  enter_cleaning();

  /* Two clean passes with no retained topics make the cleanup module report
   * done on its own; stepping the fake clock keeps the convergence
   * deterministic. If the phase machine stopped calling
   * ha_discovery_cleanup_run(), this would never leave Cleaning. */
  for (int i = 0;
       i < 200 && lifecycle_.phase() == DiscoveryLifecycleManager::kPhaseCleaning;
       i++) {
    esphome_hal_double_set_millis(esphome_hal_double_get_millis() + 500);
    lifecycle_.loop();
  }

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhasePublishing, lifecycle_.phase());
  CHECK_EQUAL(ha_discovery_state_building, manager_.state);
}

/* ------------------------------------------------------------------ */
/* publishing -> recorded outcome                                      */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, successful_publish_records_state_and_arms_reboot)
{
  enter_publishing();

  /* Still in flight: nothing recorded, no reboot armed. */
  lifecycle_.loop();
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhasePublishing, lifecycle_.phase());
  CHECK_EQUAL(0, hooks_.save_calls);

  end_publish_run(ha_discovery_state_complete);

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseRebootPending, lifecycle_.phase());
  CHECK_EQUAL(1, hooks_.confirm_calls);
  CHECK_EQUAL(1, hooks_.save_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kRecordVersion, hooks_.saved.version);
  CHECK_EQUAL(ha_discovery_manager_data_hash(&manager_), hooks_.saved.hash);
  CHECK(strcmp(hooks_.saved.device_id, identity_.get_device_id()) == 0);
  CHECK_TRUE(hooks_.saved.appliance_api_parsing);
  CHECK_TRUE(hooks_.saved.filter_config_topics);
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerNone, lifecycle_.trigger());
  CHECK(lifecycle_.initial_publish_done());
}

TEST(discovery_lifecycle_manager, failed_publish_records_nothing_and_returns_to_idle)
{
  enter_publishing();

  end_publish_run(ha_discovery_state_failed);

  /* A failed run must not be recorded: a matching stored hash is what stops
   * the next boot from retrying. */
  CHECK_EQUAL(0, hooks_.save_calls);
  CHECK_EQUAL(0, hooks_.reboot_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerNone, lifecycle_.trigger());
  CHECK(!lifecycle_.initial_publish_done());
  /* The firmware is still confirmed, so a good OTA is not rolled back. */
  CHECK_EQUAL(1, hooks_.confirm_calls);
  /* The run's buffers are released by resetting the manager to idle. */
  CHECK_EQUAL(ha_discovery_state_idle, manager_.state);
  CHECK_TRUE(manager_.decomp_buf == nullptr);
}

TEST(discovery_lifecycle_manager, a_failed_publish_can_be_retried)
{
  enter_publishing();
  end_publish_run(ha_discovery_state_failed);

  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedInitial, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, initial_publish_is_not_repeated_in_the_same_boot)
{
  enter_publishing();
  end_publish_run(ha_discovery_state_complete);
  esphome_hal_double_set_millis(DiscoveryLifecycleManager::kRebootDelayMs);
  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
  CHECK(lifecycle_.initial_publish_done());

  /* The reboot is expected never to return, but if the record never reached
   * NVS (a failed write, for example — here nothing was ever stored) the
   * latch is the only thing left that keeps a second initial publish from
   * going out in the same boot. */
  lifecycle_.check_discovery_changes(identity_.get_device_id());

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
}

/* ------------------------------------------------------------------ */
/* reboot pending -> idle                                              */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, reboot_waits_out_the_delay_and_feeds_the_watchdog)
{
  esphome_hal_double_set_millis(10000);
  enter_publishing();
  end_publish_run(ha_discovery_state_complete);

  /* Every loop iteration while waiting out the delay feeds the watchdog. */
  esphome_hal_double_set_millis(10000 + DiscoveryLifecycleManager::kRebootDelayMs / 5);
  lifecycle_.loop();
  esphome_hal_double_set_millis(10000 + DiscoveryLifecycleManager::kRebootDelayMs / 2);
  lifecycle_.loop();
  CHECK_EQUAL(0, hooks_.reboot_calls);
  CHECK_EQUAL(2, hooks_.watchdog_calls);

  esphome_hal_double_set_millis(10000 + DiscoveryLifecycleManager::kRebootDelayMs - 1);
  lifecycle_.loop();
  CHECK_EQUAL(0, hooks_.reboot_calls);
  CHECK_EQUAL(3, hooks_.watchdog_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseRebootPending, lifecycle_.phase());

  esphome_hal_double_set_millis(10000 + DiscoveryLifecycleManager::kRebootDelayMs);
  lifecycle_.loop();
  CHECK_EQUAL(1, hooks_.reboot_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, reboot_happens_once)
{
  enter_publishing();
  end_publish_run(ha_discovery_state_complete);
  esphome_hal_double_set_millis(DiscoveryLifecycleManager::kRebootDelayMs);

  lifecycle_.loop();
  lifecycle_.loop();

  CHECK_EQUAL(1, hooks_.reboot_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
}

/* ------------------------------------------------------------------ */
/* hung modules: the phase timeout returns the machine to Idle         */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, cleaning_phase_times_out_and_can_be_retried)
{
  enter_cleaning();
  /* Steer the cleanup module into a run that never reports done, e.g. a
   * broker connection dropped mid-run. */
  manager_.cleanup.state = ha_cleanup_state_cleaning;

  /* Just under the budget: still cleaning. */
  esphome_hal_double_set_millis(DiscoveryLifecycleManager::kPhaseTimeoutMs - 1);
  lifecycle_.loop();
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseCleaning, lifecycle_.phase());

  esphome_hal_double_set_millis(DiscoveryLifecycleManager::kPhaseTimeoutMs);
  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerNone, lifecycle_.trigger());
  /* destroy() memsets the cleanup struct back to idle. */
  CHECK_EQUAL(ha_cleanup_state_idle, manager_.cleanup.state);
  /* No publish ran, so nothing is recorded and the firmware is not
   * confirmed: confirmation belongs to the publish outcome. */
  CHECK_EQUAL(0, hooks_.save_calls);
  CHECK_EQUAL(0, hooks_.confirm_calls);
  CHECK(!lifecycle_.initial_publish_done());

  /* A fresh request is accepted again and reaches Cleaning: the abort left
   * the cleanup module reusable. */
  lifecycle_.trigger_discovery_refresh();
  lifecycle_.loop();
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseCleaning, lifecycle_.phase());
}

TEST(discovery_lifecycle_manager, publishing_phase_times_out_and_retries_next_boot)
{
  enter_publishing();

  /* Just under the budget: the run is still in flight. */
  esphome_hal_double_set_millis(DiscoveryLifecycleManager::kPhaseTimeoutMs - 1);
  lifecycle_.loop();
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhasePublishing, lifecycle_.phase());

  esphome_hal_double_set_millis(DiscoveryLifecycleManager::kPhaseTimeoutMs);
  lifecycle_.loop();

  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
  CHECK_EQUAL(DiscoveryLifecycleManager::kTriggerNone, lifecycle_.trigger());
  CHECK_EQUAL(0, hooks_.save_calls);
  CHECK_EQUAL(0, hooks_.reboot_calls);
  /* The firmware is still confirmed, same contract as a failed run. */
  CHECK_EQUAL(1, hooks_.confirm_calls);
  /* The run's allocations are released by resetting the manager to idle. */
  CHECK_EQUAL(ha_discovery_state_idle, manager_.state);
  CHECK(!lifecycle_.initial_publish_done());

  /* Nothing was recorded, so the next boot's change check retries. */
  lifecycle_.check_discovery_changes(identity_.get_device_id());
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseQueuedInitial, lifecycle_.phase());
}

/* ------------------------------------------------------------------ */
/* full paths                                                          */
/* ------------------------------------------------------------------ */

TEST(discovery_lifecycle_manager, cleanup_path_publishes_and_reboots)
{
  enter_cleaning();
  manager_.cleanup.state = ha_cleanup_state_done;
  lifecycle_.loop();
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhasePublishing, lifecycle_.phase());

  end_publish_run(ha_discovery_state_complete);
  esphome_hal_double_set_millis(DiscoveryLifecycleManager::kRebootDelayMs);
  lifecycle_.loop();

  CHECK_EQUAL(1, hooks_.save_calls);
  CHECK_EQUAL(1, hooks_.reboot_calls);
  CHECK_EQUAL(DiscoveryLifecycleManager::kPhaseIdle, lifecycle_.phase());
  /* The refresh path does not set the initial-publish latch. */
  CHECK(!lifecycle_.initial_publish_done());
}

TEST(discovery_lifecycle_manager, initial_path_skips_cleanup)
{
  enter_publishing();

  CHECK_EQUAL(ha_cleanup_state_idle, manager_.cleanup.state);
}
