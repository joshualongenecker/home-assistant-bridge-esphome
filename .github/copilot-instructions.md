# Copilot Instructions

## Pull Request Guidelines

- When a PR is updated with a new commit, the PR description and title must be updated to reflect the context of **all changes in the PR**, not just the changes from the latest commit or request.

## Never Merge PRs

**You must never merge a PR yourself. Only the user can merge a PR.**

- Never run `gh pr merge`, `git merge`, or any command that merges a PR branch into the target branch.
- Never delete a PR branch after merging.
- If asked to "commit and push", only commit to the feature branch and push it. Stop there.

## Never Force Push

**You must never force push (`git push --force` or `git push --force-with-lease`) unless the user explicitly instructs you to do so.**

- Always push regular commits to feature branches.
- If squashing is needed, ask the user first.
- Never rewrite shared history without explicit permission.

## Verify ESPHome Compilation Before Committing

**After making changes to component source files, verify the code compiles in the actual ESPHome build environment, not just the test harness.**

The test harness (`make test`) uses mocked ESP-IDF stubs and may not catch header issues that surface in a real ESPHome build (e.g., file-scope variables like `TAG` that exist in the .cpp but not in the header context).

Before committing changes to `.h` or `.cpp` files in `components/geappliances_bridge/`:

```bash
esphome compile /path/to/device.yaml
```

If the ESPHome build is not available, at minimum verify that any template or inline function in a header file does not reference file-scope variables (`TAG`, static globals) defined only in a .cpp file. Use literal strings or `static constexpr` instead.

## HA Discovery Pipeline

**Any change to ERD definitions, overrides, or pipeline scripts requires a full pipeline rerun before committing.**

After modifying any of these files, run:

```bash
python3 scripts/ha_discovery/run_pipeline.py
```

Then commit **all** generated/changed files together:
- `scripts/ha_discovery/appliance_api_erd_definitions_processed.json`
- `ha_discovery/*.jsonl`
- `components/geappliances_bridge/ha_discovery_data.h`

### Adding Overrides

If an ERD requires a manual override (wrong domain, missing unit, incorrect scaling, forced classification), add it to the `OVERRIDES` dict in `scripts/ha_discovery/pipeline/post_process.py`. Overrides are reapplied after auto-detection so they survive subsequent pipeline runs.

Override keys support two formats:

```python
OVERRIDES = {
    # Bare erd_id — applies to all fields in the ERD (safe for single-field ERDs).
    "0x7130": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},

    # erd_id:offset — applies only to the field at the given byte offset.
    "0x3015:0": {"unit_of_measurement": "gal/min", "scaling_factor": 10000, "field_name": "Inlet Flow Rate"},
}
```

Valid override value keys:

| Key | Description |
|-----|-------------|
| `ha_domain` | Override the Home Assistant domain (e.g. `"sensor"`, `"number"`, `"switch"`) |
| `device_class` | Override the device class (e.g. `"temperature"`, `"current"`, `"weight"`) |
| `unit_of_measurement` | Override the unit string (e.g. `"rpm"`, `"gal/min"`, `"CFM"`) |
| `scaling_factor` | Override the scaling factor (use `None` to remove scaling) |
| `state_class` | Override the state class (`"measurement"`, `"total"`, `"total_increasing"`) |
| `field_name` | Override the display name |
| `paired_erd` | Manually pair a request/status ERD (e.g. `"0x7708"`) |
| `pair_role` | Role in a pair (`"request"` or `"status"`) |
| `force_classification` | Force a classification strategy (e.g. `"single"`) |
| `value_template` | Custom Jinja2 template for value processing |

After adding an override, run the pipeline and commit all generated files.

## Integration Tests

**Before committing and pushing, run both the regular tests and the integration tests to validate they pass.**

```bash
make test -j4
make integration-test -j4
```

The regular `test` target runs 440 unit tests on CI (the full 537-test suite minus the 97 startup integration tests). The `integration-test` target runs all 537 tests (including the startup integration test suite) locally. The integration tests are excluded from CI because CppUTest's `TestMemoryAllocator` causes false "Memory corruption" detections on GCC/Ubuntu CI with `std::function` members — this is a known limitation. Always verify integration tests pass locally before pushing.

## Global Protocol Rules

**Wire-format facts that hold across the whole component. Code that appears to violate them is usually the bug — confirm against the appliance protocol before "fixing" either side.**

- **A board address is always 1 byte (`uint8_t`).** Physical board addresses are `0x00`–`0xFE`. `0xFF` is the GEA broadcast address (`tiny_gea_broadcast_address` in `lib/tiny-gea-api/include/tiny_gea_constants.h`) and can never be a physical board address. `0xFF` is used as a "primary board" sentinel in the write path and the polling list (`PROBE_ENTRY_DEFAULT_ADDRESS` in `erd_bridge_common.h`); the ERD cache stores only physical addresses.

- **An ERD is always 2 bytes (`uint16_t`, `tiny_erd_t` in `lib/tiny-gea-api/include/tiny_erd.h`).** ERD IDs are printed as 4 hex digits (`0x%04x`) in MQTT topics and logs.

- **MQTT topic format is `geappliances/{device_id}/erd/0x{ERD}/{op}` for the primary board and `geappliances/{device_id}/erd/0x{ADDR}_0x{ERD}/{op}` for secondary boards**, where `{ADDR}` is 2 hex digits, `{ERD}` is 4 hex digits, and `{op}` is `value`, `write`, or `write_result`. The no-prefix primary form is the legacy format that single-board appliances must keep working.

## Critical Invariants

**These are load-bearing contracts in the C++ component. Breaking any of them causes subtle concurrency bugs, protocol violations, or memory corruption. Read the referenced file before changing the code that implements an invariant.**

- **`erd_cache_t` is mutex-protected; entries are keyed by `(erd, board_address)` in a fixed 300-slot array backed by a 4096-byte bump arena.**
  Entries are never removed and the arena never shrinks, but an existing entry's data IS updated in place (memcpy into its arena slice). Lookup (`erd_cache_find`) is an O(n) linear scan — there is no index. A single mutex guards `entries[]` + `arena[]` + `arena_offset`, held only for a short read/copy/write, never across the MQTT publish. See `erd_cache.h` / `erd_cache.cpp`.

- **The MQTT publisher runs on Core 0 at priority 1 and publishes from a snapshot.**
  It is pinned to Core 0 (off ESPHome's loop core) at priority 1 (below the main loop) so it cannot starve the loopTask watchdog. It never mutates the cache directly — each publish atomically snapshots the payload out of the arena under the cache lock, so the main loop cannot overwrite it mid-publish. The state mutex is never held across the slow MQTT publish. See `erd_cache_mqtt_publisher.cpp`.

- **HA discovery cleanup uses a per-instance `portMUX_TYPE` (spinlock) for thread safety.**
  The cleanup struct embeds a `portMUX_TYPE` that is initialized in `ha_discovery_cleanup_init()` and re-initialized after the discovery buffers are freed (freeing re-zeros the embedded spinlock to an invalid state). See `ha_discovery_cleanup.h` / `.cpp` and `ha_discovery_manager.cpp`.

- **The ERD data size limit is 248 bytes — the GEA3 max payload (255 on-wire minus 7 bytes overhead).**
  `erd_cache_t` rejects entries whose `data_size` exceeds `ERD_CACHE_MAX_DATA_SIZE` (248). Do not grow this without confirming the appliance protocol supports a larger payload. See `erd_cache.h`.

- **Multi-board keying uses `(erd, board_address)`; the cache stores the physical board address, and the primary host stores its detected address.**
  The MQTT publisher maps the primary host's detected address (its `primary_board_address`) to the legacy no-prefix topic; every other address gets the `0x{addr}_0x{erd}` prefix. The write path uses the broadcast address `0xFF` as the "primary board" sentinel (it can never be a physical board address, keeping a `0x00` secondary board unambiguous). The polling list is a separate concept: `probe_entry_t.board_address` still uses `0xFF` (`PROBE_ENTRY_DEFAULT_ADDRESS`) to mean "use the host address". Any cache lookup or key must include the board address. See `erd_cache.h`, `erd_poll_list_builder.h`, `erd_cache_mqtt_publisher.h`.

- **Generated files must not be hand-edited.**
  `erd_lists.{h,cpp}`, `appliance_api_feature_lists.{h,cpp}`, `appliance_type_map.h`, `ha_discovery_data.{h,cpp}`, `ha_discovery/*.jsonl`, and `appliance_api_erd_definitions_processed.json` are produced by `scripts/generate_erd_lists.py` and `scripts/ha_discovery/run_pipeline.py`. CI enforces this via `scripts/check_generated_sync.sh` (run `make check-generated` locally).

## Software Factory

Features and bugs flow through a staged lifecycle: issue → triage → grill → spec → tickets → implement → review (five sub-agent axes) → device verify → PR. The lifecycle source of truth is `docs/factory.md`; the stage skills are in `skills/` (`grilling`, `grill-with-docs`, `triage`, `to-spec`, `to-tickets`, `implement`, `code-review`, `tdd`, `diagnosing-bugs`, `pr`, `domain-modeling`, `factory`).

- When working a factory stage, read and follow `skills/<stage>/SKILL.md` — the skills are agent-agnostic markdown; Copilot follows them directly from the files.
- Use the terminology from `docs/GLOSSARY.md` in all code, specs, issues, and PRs. When code and glossary disagree, the code wins — fix the glossary.
- Record hard-to-reverse decisions as ADRs in `docs/adr/` (see `docs/adr/README.md` for when to write one).
- Firmware-facing changes (diff touches `components/` or `lib/`) require test-bench device verification before the PR; the loop is in the `device-verify` skill (workstation-local).
- The agent never merges PRs and never answers its own design decisions — decisions go to the user.
