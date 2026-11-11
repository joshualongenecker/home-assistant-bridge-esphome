# Discovery Refresh Button

## Purpose

ESPHome button component that triggers a Home Assistant discovery cleanup, republishes fresh discovery topics, and restarts the device. When pressed, the request is queued — if the bridge is not yet ready (steady state, MQTT connected, device ID complete), it waits until ready before starting cleanup. After cleanup, fresh discovery topics are published, then the device reboots to defragment the heap.

## Public API

| Function | Description |
|----------|-------------|
| `DiscoveryRefreshButton(bridge)` | Constructor. Stores a pointer to the `GeappliancesBridge` instance. |
| `press_action()` override | Called by ESPHome when the button is pressed. Delegates to `bridge_->trigger_discovery_refresh()`. |

## Class

```cpp
class DiscoveryRefreshButton : public button::Button {
 public:
  DiscoveryRefreshButton(GeappliancesBridge* bridge) : bridge_(bridge) {}

  void press_action() override {
    if (bridge_ != nullptr) {
      bridge_->trigger_discovery_refresh();
    }
  }

 private:
  GeappliancesBridge* bridge_;
};
```

## Trigger Flow

`GeappliancesBridge::trigger_discovery_refresh()` is a **public** method (no `friend` declaration needed). It delegates to `discovery_lifecycle_manager_.trigger_discovery_refresh()`, which atomically claims the machine: a CAS moves `phase_` (owned by `DiscoveryLifecycleManager`) from `kPhaseIdle` to the transient `kPhaseClaimed`, then sets `trigger_` to `kTriggerDiscoveryRefresh` and publishes `kPhaseQueuedCleanup` with a release store on `phase_`, logging that the request is queued. If the CAS fails (any other phase, including a concurrent claim), it logs a warning and returns. It does **not** perform readiness checks or start cleanup immediately — the request is queued and will execute once the bridge is ready.

In the bridge's `loop()`, `discovery_lifecycle_manager_.loop()` is called, which advances the phase machine by at most one transition per call:

1. **Wait for readiness:** While `phase_` is `kPhaseQueuedCleanup`, the manager waits until the bridge's `is_ready` callback reports `steady_state_reached_` and `mqtt_client_adapter_initialized_`, and `device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE`.
2. **Start cleanup:** Once ready, `ha_discovery_cleanup_configure()` and `ha_discovery_cleanup_start()` are called and `phase_` moves to `kPhaseCleaning`.
3. **Drive cleanup:** `ha_discovery_cleanup_run()` processes cleanup work each loop iteration.
4. **On cleanup complete:** The cleanup module is destroyed, `phase_` moves to `kPhasePublishing`, and fresh discovery topics are published via `ha_discovery_manager_configure()` and `ha_discovery_manager_start()`.
5. **Drive discovery publishing:** `ha_discovery_manager_run()` publishes discovery payloads each loop iteration.
6. **On discovery complete:** `finish_publish_()` runs the `confirm_firmware` hook (clears the safe mode counter and cancels OTA rollback), saves the discovery record, and moves `phase_` to `kPhaseRebootPending`. A failed run skips the record and the reboot and returns `phase_` to `kPhaseIdle`.
7. **Wait then reboot:** After a 5-second delay, `esphome::App.safe_reboot()` performs a graceful reboot (disconnects from MQTT before resetting).

The cleanup → publish → reboot flow is driven by `DiscoveryLifecycleManager`. The same path is used for automatic discovery change detection (hash or device ID change detected at steady state) and fresh install discovery publish.

## ESPHome Configuration

The button is auto-created by default in `__init__.py`:

```yaml
geappliances_bridge:
  discovery_refresh_button: true  # default; set to false to disable
```

Or with custom options:

```yaml
geappliances_bridge:
  discovery_refresh_button:
    name: "Discovery Refresh"
    disabled_by_default: false
```

## Dependencies

- `esphome/components/button/button.h` — ESPHome `button::Button` base class
- `geappliances_bridge.h` — `GeappliancesBridge` class (public `trigger_discovery_refresh()` delegates to `DiscoveryLifecycleManager`)
- `discovery_lifecycle_manager.h` — `DiscoveryLifecycleManager` owns the cleanup → publish → reboot phase machine
- `ha_discovery_cleanup.h` — cleanup module for removing stale MQTT discovery messages
- `device_identity_manager.h` — provides the device ID for cleanup configuration

## Key Design Decisions

- **Thin wrapper**: The class is a minimal adapter between ESPHome's button component and the bridge's `trigger_discovery_refresh()` method. All logic (queuing, cleanup, republish, restart) lives in `DiscoveryLifecycleManager`, delegated through the bridge.
- **Null-safe**: The `press_action()` method checks `bridge_ != nullptr` before calling through, protecting against use-after-free if the bridge is destroyed before the button.
- **Auto-created**: The button is created by default (`discovery_refresh_button: true`) so users get the functionality without explicit configuration. It can be disabled by setting `discovery_refresh_button: false`.
- **Queued execution**: The request is queued if pressed before the bridge is ready. This eliminates guard checks and allows the user to press the button at any time.
- **Shared cleanup path**: Discovery Refresh, automatic discovery change detection (hash/device_id change at steady state), and fresh install all share the same cleanup → publish → reboot flow (the `kPhaseQueuedCleanup` → `kPhaseCleaning` → `kPhasePublishing` → `kPhaseRebootPending` sequence owned by `DiscoveryLifecycleManager`). A fresh install enters through `kPhaseQueuedInitial` and skips the cleanup phase.
- **`safe_reboot()` instead of `App.reboot()`**: Uses `App.safe_reboot()` to gracefully disconnect from MQTT before resetting, ensuring a clean session end.
- **Firmware confirmed before rebooting**: the `confirm_firmware` hook clears the safe mode boot loop counter and cancels OTA rollback, preventing the device from entering safe mode due to rapid reboots. It runs even when the discovery run failed, so a good OTA is never rolled back because discovery failed.
- **5-second pre-reboot delay**: Allows final discovery messages to transmit and the heap to stabilize before rebooting. The reboot also defragments the heap after the memory-intensive cleanup and publish cycle.