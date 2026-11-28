# Development Guide

Build, test, and debug the GE Appliances Bridge.

## Prerequisites

### Build Tools

- **ESP-IDF** (v5.3+) — for firmware compilation. Follow the [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/).
- **ESPHome** — for the build and flash workflow: `pip install esphome`.
- **Python 3.10+** — for pipeline scripts and tests.
- **CppUTest** — for unit tests:
  - Ubuntu/Debian: `sudo apt-get install cpputest libcpputest-dev`
  - macOS: `brew install cpputest`

### Clone

```bash
git clone --recursive https://github.com/joshualongenecker/home-assistant-bridge-esphome.git
cd home-assistant-bridge-esphome
```

The `--recursive` flag is required — the GEA protocol stack lives in `lib/tiny` and `lib/tiny-gea-api` submodules.

## Build

### Firmware (ESPHome)

```bash
esphome run gea-bridge.yaml
```

This compiles the component with ESP-IDF and flashes it to the connected adapter.

### Unit Tests (Host)

```bash
make test
```

Compiles and runs the CppUTest suite. Tests run on the host (not the ESP32) using stubs for ESP-IDF APIs.

Both suites run under AddressSanitizer and UndefinedBehaviorSanitizer. On Linux, LeakSanitizer is on too, so a leaked allocation fails the run. On macOS the Makefile turns leak detection off, because Apple clang's sanitizer runtime has no LeakSanitizer; leaks are caught by CI on Linux instead. `make test TSAN=1` is Linux-only for the same reason.

### Python Tests

```bash
make pytest
```

Runs every `scripts/test_*.py`: pipeline, HA discovery, discovery compression, ERD-list generation, and the comment-policy gate itself.

### Comment Ratchet

```bash
make check-comments
```

Counts the comments CI is still willing to tolerate, per file, against `scripts/comment_baseline.json`. A file that gains a comment fails the check; the fix is to delete it, move the rationale into an ADR or the module spec, or (for a genuine tool directive or API docstring) leave it — those two kinds are exempt and never counted. See [Comment Policy](../comment-policy.md).

## Debug

### ESPHome Logs

```bash
esphome logs gea-bridge.yaml
```

Shows real-time serial output from the adapter.

### Enable Debug Logging

Add to your YAML:

```yaml
logger:
  level: DEBUG
  logs:
    geappliances_bridge: DEBUG
```

### GDB on ESP-IDF

```bash
idf.py monitor
# In the monitor: Ctrl+] to access serial port, then:
# monitor reset
# monitor gdb
```

In another terminal:

```bash
xtensa-esp32c3-elf-gdb build/gea-bridge.elf
(target remote) :3333
```

## Test Infrastructure

### Unit Tests (`test/`)

CppUTest-based tests for individual modules. Each module has a corresponding test file in `test/tests/`.

### Simulation Tests (`test/simulation/`)

Application-level integration tests that simulate complete workflows without physical hardware. Uses test doubles for the GEA3 ERD client, MQTT client, and timer system.

See [test/simulation/README.md](../../test/simulation/README.md) for details.

### Pipeline Tests (`scripts/`)

Python tests for the HA discovery pipeline:
- `test_pipeline.py` — validates pipeline stages produce correct output.
- `test_ha_discovery.py` — validates discovery payload generation.
- `test_compress_ha_discovery.py` — validates compression/decompression roundtrip.

### Test Doubles and Harness Conventions

- **Callbacks are function pointers plus a `void* context`, never `std::function`** — `DiscoveryLifecycleManager::Hooks`, `ha_discovery_manager_t::get_time_ms`, `erd_bridge_poll_t::yield_fn`; `publish_sensor_group` in `diagnostic_sensor_publisher.cpp` is templated for the same reason (197). CppUTest's `TestMemoryAllocator` reported false "Memory corruption" detections for `std::function` members, which is why the integration suite was once excluded from CI; with none left, `make test`, `make integration-test`, the `-ftrivial-auto-var-init=pattern` variant and `TSAN=1` all run.
- **No `mock().clear()` or `mock().clearAllData()` in tests.** A group's `teardown()` runs before `MockSupportPlugin::postTestAction()`, so a clear there discards pending expectations and an unmet `expectOneCall` passes silently. `MockClearGuardPlugin` in `test/test_runner.cpp` fails any test that clears the store, and is installed **first** because `preTestAction` runs in reverse install order and `postTestAction` in install order (2815, 2816). Where the path under test bypasses the mock, assert on the double's recorded state (e.g. `last_published_topic_`) or call `mock().checkExpectations()` at the end of the test body.
- **`tiny_timer` and `tiny_event` callbacks must be `static`** (`AutodiscoveryManager::timer_callback_`, `FeatureBitManager::parse_timer_callback_`, `FeatureBitManager::queue_retry_timer_callback_`): the C APIs take a free function plus a context pointer, and the object is recovered by cast inside the callback (151, 1119, 1120).
- **Time-based logic needs an injected clock.** The HAL double's `millis()` returns `esphome_hal_double_get_millis()`, a global frozen at 0 unless a test calls `esphome_hal_double_set_millis()`, so budgets, backoff windows and yield points never execute under test. Use `erd_bridge_poll_set_time_fn` / `erd_bridge_poll_set_yield_fn`, `erd_cache_mqtt_publisher_set_time_fn` or `ha_discovery_manager_set_time_fn`, and only **after** `init()` — `init()` installs the HAL default unconditionally and a NULL argument restores it. Tests install an advancing fake (+10 ms per call) and a counting yield fake to pin the `yield_fn()` call site. (3235, 3240, 3245, 3246, 3254, 3255)
- **Run the phony `make integration-test` target, never the binary target directly.** The target re-invokes make with `TARGET=$(TARGET_INTEGRATION)` and `SRCS=$(SRCS_INTEGRATION)`; the default `OBJS` expand from an `SRCS` that excludes `startup_integration_test.cpp`, so building `build/home-assistant-bridge_tests_integration` directly links a stale object set and passes silently. The two targets keep separate output binaries so they can never run each other's stale binary (8, 9, 10, 11).

## HA Discovery Pipeline

Any change to ERD definitions, overrides, or pipeline scripts requires a full pipeline rerun:

```bash
python3 scripts/ha_discovery/run_pipeline.py
```

This regenerates:
- `scripts/ha_discovery/appliance_api_erd_definitions_processed.json`
- `ha_discovery/*.jsonl`
- `components/geappliances_bridge/ha_discovery_data.h`

**Commit all generated files together with your changes.**

See [Pipeline Guide](./pipeline.md) for details on adding overrides and debugging pipeline issues.

## Code Structure

```
components/geappliances_bridge/   # Main component source
lib/tiny/                          # GEA protocol stack (submodule)
lib/tiny-gea-api/                  # GEA API abstractions (submodule)
test/                              # Unit tests
scripts/ha_discovery/              # HA discovery pipeline
docs/                              # Documentation
```

## Contributing

See [CONTRIBUTING.md](../../CONTRIBUTING.md) for the PR process and code style.