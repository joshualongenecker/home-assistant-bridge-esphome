# Bridge Mode — Specification

## 1. Overview

### 1.1 Purpose

Defines the `BridgeMode` enumeration as a standalone, shared type so it can be referenced by both `IBridgeServices` and `GeappliancesBridge` without creating a circular dependency.

### 1.2 Responsibilities

- Declare the `BridgeMode` enum with three values: `BRIDGE_MODE_POLL`, `BRIDGE_MODE_SUBSCRIBE`, `BRIDGE_MODE_AUTO`.
- Provide the mapping comment tying enum values to `__init__.py` constants.

### 1.3 Not Responsible For

- Any logic, state, or behaviour related to bridge mode selection or switching.
- Runtime mode negotiation or fallback logic (owned by the startup sequence and bridge implementations).

---

## 2. Interface

### 2.1 Enum

```c
enum BridgeMode {
  BRIDGE_MODE_POLL      = 0,  // Always use polling mode
  BRIDGE_MODE_SUBSCRIBE = 1,  // Always use subscription mode
  BRIDGE_MODE_AUTO      = 2   // Auto: try subscription, fallback to polling
};
```

The enum is declared within the `esphome::geappliances_bridge` namespace.

### 2.2 Enum Values

| Value | Constant | Description |
|-------|----------|-------------|
| `0` | `BRIDGE_MODE_POLL` | Always use polling mode. The bridge reads ERD values on a schedule rather than relying on appliance-pushed updates. |
| `1` | `BRIDGE_MODE_SUBSCRIBE` | Always use subscription mode. The bridge requests the appliance to push ERD updates; if the appliance does not support subscriptions, the bridge will fail rather than fall back. |
| `2` | `BRIDGE_MODE_AUTO` | Auto-detect. The bridge first attempts subscription mode; if the appliance does not support it, the bridge falls back to polling. |

### 2.3 Contract with Python Configuration

The enum values MUST match the `MODE_*_VALUE` constants defined in `__init__.py` (the ESPHome platformio component). The YAML configuration key maps to these integer values, which are then passed to the C++ layer as a `BridgeMode` enum.

---

## 3. Behavior

### 3.1 Mode Selection

#### Requirement 3.1.1: Enum Value Stability

The integer values of `BridgeMode` MUST NOT change without a corresponding update to the `MODE_*_VALUE` constants in `__init__.py`. A mismatch between the Python-side constant and the C++ enum value will cause incorrect mode selection at runtime.

**Rationale:** The bridge mode is configured in YAML, parsed by ESPHome's Python layer, and passed as an integer to the compiled component. The enum serves as the single source of truth for the mapping.

**Implementation:** `components/geappliances_bridge/bridge_mode.h` lines 26–30.

**Verification:** Confirm that `MODE_POLL_VALUE`, `MODE_SUBSCRIBE_VALUE`, and `MODE_AUTO_VALUE` in `__init__.py` match `0`, `1`, and `2` respectively.

---

## 4. Notes

1. **Standalone header to break circular dependency.** `IBridgeServices` and `GeappliancesBridge` both need to reference `BridgeMode`, but neither can include the other's header. This header has no dependencies and can be included by either party.
2. **The enum values are duplicated in Python and nothing ties the two copies together.** `bridge_mode.h` declares `BRIDGE_MODE_POLL = 0`, `BRIDGE_MODE_SUBSCRIBE = 1`, `BRIDGE_MODE_AUTO = 2`; `__init__.py` lines 56–58 declare `MODE_POLL_VALUE = 0`, `MODE_SUBSCRIBE_VALUE = 1`, `MODE_AUTO_VALUE = 2`, and the `CONF_MODE` schema (lines 157–163) maps the YAML strings `poll` / `subscribe` / `auto` to those integers. `__init__.py:299` emits `cg.add(var.set_mode(config[CONF_MODE]))`, and `set_mode(uint8_t mode)` (`geappliances_bridge.h:82`) does `static_cast<BridgeMode>(mode)` with no range check. **Known gap:** there is no compile-time or test-time tie between the two — the C++ enumerators are invisible to the Python layer, and no `static_assert`, generated constant, or test compares them, so the only thing keeping them aligned is the mirror comment on each side. (An issue is being filed separately; this spec deliberately does not fix it.)

3. **What a mismatch actually does at runtime.** `initialize_erd_bridge_()` (`geappliances_bridge_bridge_init.cpp:173-187`) dispatches on equality against the three enumerators. A value that matches none leaves `use_polling = false` and `mode_name = "unknown"`, so the bridge silently starts the **subscription** path and logs `Bridge mode: unknown` (line 190). A drifted value (Python sending 2 where C++ AUTO is 1) selects `BRIDGE_MODE_SUBSCRIBE` instead of AUTO with no error anywhere — which is why the duplication is load-bearing rather than cosmetic.

4. **The default is duplicated as well.** `GeappliancesBridge::mode_` is initialized to `BRIDGE_MODE_AUTO` (`geappliances_bridge.h:232`) and the Python schema default is `MODE_AUTO` → `MODE_AUTO_VALUE` = 2. Both copies must move together, or a config that omits `mode:` behaves differently from the documented default.

5. **Mode semantics, including where they are overridden.** `BRIDGE_MODE_POLL` always polls; `BRIDGE_MODE_SUBSCRIBE` subscribes and does **not** fall back; `BRIDGE_MODE_AUTO` attempts subscription and falls back to polling. Concretely, `erd_poll_list_builder.cpp:78-79` treats the poll list as subscription-shaped when the mode is `BRIDGE_MODE_SUBSCRIBE`, or `BRIDGE_MODE_AUTO` with `subscription_active`; and a GEA2 bus forces polling regardless of the configured mode (`geappliances_bridge_bridge_init.cpp:176-178`, `polling (GEA2 - subscriptions not supported)`).

6. **The standalone header has three consumers, not two.** `bridge_mode.h` is included by `i_bridge_services.h`, `geappliances_bridge.h` and `erd_poll_list_builder.h`. It MUST stay free of includes: it is the break in the `IBridgeServices` ↔ `GeappliancesBridge` dependency cycle, and anything added to it propagates into all three.