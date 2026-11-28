# Discovery Refresh Button — Specification

## 1. Overview

### 1.1 Purpose

The Discovery Refresh Button is an ESPHome `button::Button` component that triggers a Home Assistant discovery cleanup, republishes fresh discovery topics, and reboots the device when pressed. It provides a user-accessible mechanism to force the bridge to clear all retained discovery messages from the MQTT broker, publish fresh ones, and reboot — causing Home Assistant to rediscover the device from scratch on the next boot.

### 1.2 Responsibilities

- Expose a single press action via the ESPHome Button component interface
- Guard against null bridge pointer before delegating
- Delegate the actual cleanup logic to `GeappliancesBridge::trigger_discovery_refresh()`

### 1.3 Not Responsible For

- Discovery cleanup execution (delegated to `GeappliancesBridge`)
- MQTT message publishing (delegated to `ha_discovery_cleanup` module)
- Device reboot scheduling (delegated to `GeappliancesBridge` loop)
- Button lifecycle management (owned by ESPHome's component framework)

---

## 2. Class Hierarchy

```
esphome::button::Button          (abstract base — provides press_action() interface)
    └── esphome::geappliances_bridge::DiscoveryRefreshButton
```

The class extends `button::Button` and implements the pure virtual `press_action()` method. The base class provides:

- Virtual destructor
- `press_action()` — pure virtual, called when the button is pressed
- `add_on_press_callback()` — registers callbacks invoked on press
- `press()` — test helper that fires all registered callbacks

---

## 3. Public API

### 3.1 Constructor

```cpp
DiscoveryRefreshButton(GeappliancesBridge* bridge) : bridge_(bridge) {}
```

| Parameter | Description |
|-----------|-------------|
| `bridge` | Raw pointer to the owning `GeappliancesBridge` instance. May be `nullptr` (guarded against at press time). |

The constructor stores the bridge pointer for later use during `press_action()`. No initialization, validation, or side effects are performed.

### 3.2 press_action()

```cpp
void press_action() override {
  if (bridge_ != nullptr) {
    bridge_->trigger_discovery_refresh();
  }
}
```

Called by ESPHome's button framework when the button is pressed. Delegates to `GeappliancesBridge::trigger_discovery_refresh()` after a null guard on `bridge_`.

---

## 4. Integration with GeappliancesBridge

### 4.1 Bridge Pointer

The button holds a raw pointer (`GeappliancesBridge* bridge_`) to the bridge instance. The pointer is:

- Set once at construction time
- Never modified after construction
- Not reference-counted or owned by the button

The bridge is expected to outlive the button in normal operation, as both are managed by ESPHome's component lifecycle.

### 4.2 trigger_discovery_refresh() Flow

When `press_action()` delegates to `bridge_->trigger_discovery_refresh()`, the bridge delegates to `discovery_lifecycle_manager_.trigger_discovery_refresh()`. The `DiscoveryLifecycleManager` performs:

1. **Idempotency guard (atomic claim):** `phase_` (owned by `DiscoveryLifecycleManager`) is an atomic byte. The manager atomically compares-and-swaps it from `kPhaseIdle` to the transient `kPhaseClaimed`; any other value logs a warning naming the request and the current phase ("Discovery refresh ignored: discovery is publishing") and returns. The claimer then writes `trigger_` and publishes `phase_` as the queued phase, so the release store publishes the trigger with it. The claim is atomic because `press_action()` is MQTT-command-driven and may run off the main loop: a plain check-then-act could let two concurrent triggers both pass the Idle check and overwrite each other's trigger.
2. **Queue the request:** On a successful claim, `trigger_` is set to `kTriggerDiscoveryRefresh` and `phase_` to `kPhaseQueuedCleanup`, then "Discovery refresh queued, will execute when appliance is ready" is logged. `loop()` treats the transient `kPhaseClaimed` as busy and waits one iteration for the queued phase to appear.

No guard checks for steady state, MQTT readiness, or device ID are performed at press time. The request is queued and will execute in `DiscoveryLifecycleManager::loop()` once all prerequisites are met. This allows the user to press the button at any time, even during startup.

### 4.3 Queued Execution in DiscoveryLifecycleManager::loop()

The bridge's `loop()` calls `discovery_lifecycle_manager_.loop()`, which advances the phase machine by at most one transition per call. With `phase_` at `kPhaseQueuedCleanup`, it waits for readiness and then starts the cleanup:

1. **Wait for readiness:** The manager calls the `is_ready` callback the bridge passed to `init()` — `GeappliancesBridge::discovery_is_ready_`, which reports `steady_state_reached_` and `mqtt_client_adapter_initialized_` — and checks `device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE`. It stays in `kPhaseQueuedCleanup` until both hold.
2. **Start cleanup:** Once ready, `ha_discovery_cleanup_configure()` and `ha_discovery_cleanup_start()` are called and `phase_` moves to `kPhaseCleaning`. This hands off to the shared cleanup → publish → reboot path.
3. **Drive cleanup:** `ha_discovery_cleanup_run()` is called each loop iteration. When done, the cleanup module is destroyed and `phase_` moves to `kPhasePublishing`, where fresh discovery topics are published. If the cleanup stops removing topics (e.g. the broker connection drops mid-run), the phase is aborted after `kPhaseTimeoutMs` (5 minutes) **with no topic removed** — measured from the last removal via `ha_discovery_cleanup_total_removed()`, not from phase entry, so a congested broker that forces removal retries never aborts a cleanup that is still draining (see §8.2 of `ha_discovery_cleanup.md`). On abort the cleanup module is destroyed, no record is saved, and `phase_` returns to `kPhaseIdle` so a later Discovery Refresh or the next boot can retry.
4. **Drive discovery publishing:** `ha_discovery_manager_run()` publishes discovery payloads. When the run completes, `finish_publish_()` calls the `confirm_firmware` hook (clears the safe-mode counter and cancels OTA rollback), saves the discovery record, and moves `phase_` to `kPhaseRebootPending`. A failed run skips the record and the reboot and returns `phase_` to `kPhaseIdle`. A run that stops making progress is aborted by the same `kPhaseTimeoutMs` budget — measured from the last successful publish, not from phase entry, so a congested broker that forces retries never aborts a run that is still publishing (see §8.6 of `ha_discovery_manager.md`). The firmware is still confirmed (the discovery outcome does not invalidate the OTA), the manager is reset to idle to release the run's allocations, no record is saved, and `phase_` returns to `kPhaseIdle`.
5. **Wait then reboot:** After a 5-second delay (`kRebootDelayMs`), `phase_` returns to `kPhaseIdle` and the `reboot` hook (`esphome::App.safe_reboot()` in production) performs a graceful reboot.

**Note:** A request is accepted only from `kPhaseIdle` (via the atomic claim), so a Discovery Refresh pressed while an OTA-triggered cleanup is running is rejected by the same guard — one phase at a time.

---

## 5. State Transitions

### 5.1 Press When Bridge Is Idle (Steady State)

| Condition | Result |
|-----------|--------|
| `bridge_ != nullptr` | Delegates to `trigger_discovery_refresh()`, which forwards to `discovery_lifecycle_manager_.trigger_discovery_refresh()` |
| `phase_` (in `DiscoveryLifecycleManager`) == `kPhaseIdle` | Wins the atomic claim (`phase_` transiently `kPhaseClaimed`) |
| **Outcome** | `trigger_` set to `kTriggerDiscoveryRefresh` and `phase_` to `kPhaseQueuedCleanup` in `DiscoveryLifecycleManager`. The manager's `loop()` treats the transient `kPhaseClaimed` as busy and moves to `kPhaseCleaning` on the next call (bridge is already ready), then proceeds through cleanup → publish → reboot. |

### 5.2 Press While Refresh Already In Progress

| Condition | Result |
|-----------|--------|
| `phase_` (in `DiscoveryLifecycleManager`) != `kPhaseIdle` | **Blocked** — warning logged naming the request and the current phase, e.g. "Discovery refresh ignored: discovery is cleaning topics". No state change. |

### 5.3 Press Before Steady State Reached

| Condition | Result |
|-----------|--------|
| `steady_state_reached_ == false` (or MQTT not ready, or device ID not complete) | **Queued** — `phase_` set to `kPhaseQueuedCleanup` in `DiscoveryLifecycleManager`. The request waits in `DiscoveryLifecycleManager::loop()` until all prerequisites are met, then executes. |

### 5.4 Press With Null Bridge

| Condition | Result |
|-----------|--------|
| `bridge_ == nullptr` | **Silent no-op** — `press_action()` returns without logging or error. |

---

## 6. Safety

### 6.1 Null Bridge Guard

`press_action()` checks `bridge_ != nullptr` before calling `trigger_discovery_refresh()`. This protects against:

- Early construction before the bridge is available
- Partial initialization scenarios
- Test environments where the bridge may not be instantiated

The guard is silent — no log message is emitted when `bridge_` is null.

### 6.2 Idempotency

`trigger_discovery_refresh()` delegates to `discovery_lifecycle_manager_.trigger_discovery_refresh()`, which guards against concurrent execution via an atomic claim of `phase_` (owned by `DiscoveryLifecycleManager`): a request is accepted only by the thread that swaps `phase_` from `kPhaseIdle`, so the guard holds even when the press arrives off the main loop. Multiple rapid presses of the button are safely ignored after the first press queues the request.

### 6.3 Queued Execution

The request is queued rather than rejected if the bridge is not ready. This eliminates the need for guard checks at press time and allows the user to press the button at any time. The actual cleanup starts in `DiscoveryLifecycleManager::loop()` once the bridge's `is_ready` callback (`steady_state_reached_` and `mqtt_client_adapter_initialized_`) and device ID completion are all satisfied.

### 6.4 Shared Path with OTA

The Discovery Refresh button uses the same cleanup → publish → reboot path as OTA-triggered discovery. Both paths run through the same `kPhaseQueuedCleanup` → `kPhaseCleaning` → `kPhasePublishing` → `kPhaseRebootPending` sequence, owned by `DiscoveryLifecycleManager`. This means a Discovery Refresh request and an OTA-triggered cleanup cannot run concurrently — the first one to start owns the path.

---

## 7. Dependencies

| Dependency | Role |
|------------|------|
| `esphome/components/button/button.h` | Base class `button::Button` with `press_action()` interface |
| `geappliances_bridge.h` | `GeappliancesBridge` class with `trigger_discovery_refresh()` method |
| `esphome/core/log.h` | Logging (`ESP_LOGW`, `ESP_LOGI`) via the bridge's `trigger_discovery_refresh()` |
| `ha_discovery_cleanup.h` | Cleanup module used by the bridge to clear retained discovery messages |

---

## 8. Invariants

1. **Single bridge reference:** `bridge_` is set once at construction and never modified.
2. **No ownership:** The button does not own or manage the lifecycle of the bridge.
3. **Delegation-only behavior:** The button performs no cleanup logic itself — all work is delegated to `GeappliancesBridge::trigger_discovery_refresh()`, which forwards to `DiscoveryLifecycleManager`.
4. **Queued execution:** The request is queued at press time and executes in `DiscoveryLifecycleManager::loop()` when the bridge is ready. No guard checks for steady state, MQTT, or device ID are performed at press time.
5. **Shared cleanup path with OTA:** Both OTA reboot and Discovery Refresh use the same `kPhaseQueuedCleanup` → `kPhaseCleaning` → `kPhasePublishing` → `kPhaseRebootPending` flow (all state owned by `DiscoveryLifecycleManager`).

---

## 9. Known Limitations

1. **No visual feedback:** The button provides no indication of whether the press was accepted or rejected. The user must check logs to determine the outcome.
2. **Device reboot is mandatory:** After cleanup completes, the device always reboots. There is no option to complete cleanup without rebooting.
3. **5-second pre-reboot delay is fixed:** The delay between discovery publishing completion and reboot is hardcoded to 5 seconds. If the MQTT broker is slow to acknowledge messages, some messages may be lost.
4. **No cancellation:** Once cleanup has started, there is no way to cancel it. The device will proceed through cleanup, publish, and reboot.
5. **`safe_reboot()` for graceful disconnect:** The reboot uses `App.safe_reboot()` to gracefully disconnect from MQTT before resetting, ensuring a clean session end on the broker.

---

## 10. Notes

1. **The sibling button.** `ReconnectApplianceButton` is declared next to `DiscoveryRefreshButton` in `geappliances_bridge.h`, with the same shape: a raw non-owning `GeappliancesBridge* bridge_` set at construction and null-guarded in `press_action()`. Its press action calls `GeappliancesBridge::restart_appliance_connection()`, i.e. the appliance-restart path of ADR-0006: `destroy_data_plane_()` (subscription, polling and write bridges), `erd_cache_clear()`, `erd_registry_.clear_valid_erds()`, `feature_bit_manager_.cleanup()`, then `autodiscovery_manager_.cleanup()` + re-init and a fresh startup HSM entered at `startup_state_startup_delay`.

2. **How it differs from Discovery Refresh.** Discovery Refresh is a broker/HA-side operation: cleanup of retained discovery topics, republish, then a reboot (ADR-0003). Reconnect Appliance is an appliance-side operation and never reboots: the MQTT client, the device ID, the ERD cache object and its publisher, and the HA discovery state are all kept, so the MQTT topics and the Home Assistant device — including its entities — stay exactly as they are; only what the appliance's boards reported is discarded (ERD values, the valid-ERD filter, feature bits) and re-read from scratch. Use it after the appliance's boards were reprogrammed or reset behind the bridge's back; use Discovery Refresh when the discovery data on the broker is what is wrong.

3. **Press timing: deferred, not rejected.** `restart_appliance_connection()` only sets `restart_requested_`. `loop()` clears the flag and runs `restart_appliance_connection_now_()` only when `discovery_lifecycle_manager_.phase() == DiscoveryLifecycleManager::kPhaseIdle`, so a press during a discovery run waits for that run to finish instead of being dropped — the opposite of Discovery Refresh, whose atomic claim rejects a press outside `kPhaseIdle` (§5.2). Because only `loop()` acts on the flag, all restart work runs in the main loop and never on the MQTT command path that delivered the press. If startup has not begun (`startup_hsm_wrapper_.hsm.current == nullptr`), the restart is a deliberate no-op: startup will discover the appliance anyway.

4. **ESPHome entity wiring** (`components/geappliances_bridge/__init__.py`). Config key `reconnect_appliance_button`, auto-created by default and disabled with `false`. The schema is `button.button_schema(ReconnectApplianceButton, icon="mdi:lan-connect")` with default name "Reconnect Appliance"; codegen instantiates it with `cg.new_Pvariable(val[CONF_ID], var)` — the bridge pointer — and registers it through `button.register_button()`. The key is listed in `DIAGNOSTIC_ENTITY_KEYS`, so it validates with full entity defaults and gets the generated MQTT wrapper; an entity assembled by hand without that key is registered but never published. It is pressable from Home Assistant and from YAML with `button.press`.

5. **No press feedback, same as Discovery Refresh** (§9.1). A deferred press is invisible in the UI; the only trace is the log line "Appliance treated as lost: restarting discovery and the appliance startup" when `loop()` finally runs it.
