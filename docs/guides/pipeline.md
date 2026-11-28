# Pipeline Guide

End-to-end guide for the Home Assistant discovery pipeline.

## Overview

The pipeline converts GE Appliances ERD definitions into Home Assistant MQTT discovery entities, then compresses them into a C header embedded in the firmware.

```
appliance_api_erd_definitions.json (submodule)
    ↓
generate_flattened_review.py  →  appliance_api_erd_definitions_processed.json
    ↓
auto_detect_ha_domain.py      →  assign ha_domain
auto_detect_device_class.py   →  assign device_class
auto_detect_state_class.py    →  assign state_class
auto_detect_scaling.py        →  infer scaling_factor
auto_detect_pairings.py       →  detect Request/Status pairs
post_process.py               →  apply overrides, fix consistency
    ↓
generate_ha_discovery.py      →  ha_discovery/*.jsonl
    ↓
compress_ha_discovery.py      →  components/geappliances_bridge/ha_discovery_data.h
```

## Running the Pipeline

```bash
python3 scripts/ha_discovery/run_pipeline.py
```

**Always run this before committing changes to ERD definitions, overrides, or pipeline scripts.** It regenerates all derived artifacts.

## Adding Overrides

If an ERD needs manual correction (wrong domain, missing unit, incorrect scaling), add an override to `scripts/ha_discovery/pipeline/post_process.py`:

```python
OVERRIDES = {
    # Bare erd_id — applies to all fields in the ERD.
    "0x7130": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},

    # erd_id:offset — applies only to the field at the given byte offset.
    "0x3015:0": {"unit_of_measurement": "gal/min", "scaling_factor": 10000},
}
```

### Valid Override Keys

| Key | Description | Example |
|---|---|---|
| `ha_domain` | Home Assistant domain | `"sensor"`, `"number"`, `"switch"` |
| `device_class` | Device class | `"temperature"`, `"current"`, `"weight"` |
| `unit_of_measurement` | Unit string | `"rpm"`, `"gal/min"`, `"CFM"` |
| `scaling_factor` | Scaling factor | `100`, `10000`, or `None` to remove |
| `state_class` | State class | `"measurement"`, `"total"`, `"total_increasing"` |
| `field_name` | Display name | `"Inlet Flow Rate"` |
| `paired_erd` | Manual request/status pair | `"0x7708"` |
| `pair_role` | Role in a pair | `"request"` or `"status"` |
| `force_classification` | Force classification | `"single"` |
| `value_template` | Custom Jinja2 template | `"..."` |

Overrides are reapplied after auto-detection, so they survive subsequent pipeline runs.

## Pipeline Stages

### 1. Flatten Review

`generate_flattened_review.py` takes the raw ERD definitions and creates one entry per sub-field. Each ERD can contain multiple fields at different byte offsets.

### 2. Auto-Detection

Five scripts run sequentially to infer Home Assistant metadata:

- **`auto_detect_ha_domain.py`** — Assigns `sensor`, `number`, `switch`, `binary_sensor`, or `button` based on field type and name keywords.
- **`auto_detect_device_class.py`** — Assigns device class (`temperature`, `power`, `energy`, etc.) based on field name patterns.
- **`auto_detect_state_class.py`** — Assigns `measurement`, `total`, or `total_increasing` for sensor entities.
- **`auto_detect_scaling.py`** — Infers `scaling_factor` from field name patterns (e.g., "percentage" → divide by 100).
- **`auto_detect_pairings.py`** — Detects Request/Status ERD pairs (e.g., a setpoint request and its corresponding status).

### 3. Post-Process

`post_process.py` applies manual overrides and fixes cross-field consistency issues (e.g., ensuring paired ERDs have matching domains).

### 4. Generate Discovery

`generate_ha_discovery.py` produces one JSONL file per appliance category (`common.jsonl`, `dishwasher.jsonl`, `laundry.jsonl`, etc.). Each line is a complete Home Assistant discovery payload.

### 5. Compress

`compress_ha_discovery.py` zlib-compresses each JSONL file and embeds the compressed bytes in a C header (`ha_discovery_data.h`). The firmware decompresses these at runtime.

## Debugging Pipeline Issues

### Check Intermediate Output

The processed definitions are saved at `scripts/ha_discovery/appliance_api_erd_definitions_processed.json`. Inspect this file to verify auto-detection results before they're compiled into discovery payloads.

### Run Individual Stages

Each pipeline script can be run independently:

```bash
python3 scripts/ha_discovery/pipeline/auto_detect_ha_domain.py
python3 scripts/ha_discovery/pipeline/auto_detect_device_class.py
```

### Verify Compression Roundtrip

```bash
make pytest
```

The `test_compress_ha_discovery.py` test verifies that compressed data decompresses to the original JSONL.

### Check Output Size

The compressed header size impacts firmware flash usage. Check the size after a pipeline run:

```bash
ls -la components/geappliances_bridge/ha_discovery_data.h
```

## When to Run the Pipeline

Run the pipeline whenever you:

1. Add or modify an override in `post_process.py`.
2. Change auto-detection logic in any `auto_detect_*.py` script.
3. Update the upstream `public-appliance-api-documentation` submodule.
4. Change the discovery payload format in `generate_ha_discovery.py`.

## Decision Notes

The constraints behind the generated entities. They are recorded here, not in the pipeline sources: a generator or detector source carries no prose comments (see [ADR-0001](../adr/0001-source-comments-banned.md)). Breaking one silently changes which entities Home Assistant gets, or makes a writable entity corrupt the appliance.

Related: [ADR-0008](../adr/0008-valid-erd-filter-borrows-one-array.md) (which ERDs discovery may publish for), [ADR-0012](../adr/0012-one-owner-for-the-mqtt-topic-grammar.md) (ERD topic grammar), [ADR-0019](../adr/0019-silent-board-keeps-its-entities.md) (a silent board keeps its entities), [appliance type → category](../reference/appliance-type-categories.md) (the category token that selects the JSONL file — an unknown token is a pipeline failure, enforced by the `_unknown_categories` check at import of `generate_ha_discovery.py`).

### Entity shape and writability

- **Command-topic eligibility.** The firmware emits a `command_topic` for a discovery entity only when that entity has a `command_template` or a paired ERD; Home Assistant rejects a `switch`/`number` with neither. So an *unpaired* `switch`/`number` sub-field of a multi-field ERD is demoted at generation time (`_handle_byte_offset_erd`, `_handle_mixed_erd`): a write always carries the whole ERD payload, so the sub-field cannot be written on its own. `switch` → `binary_sensor`, `number` → `sensor`, command template and options cleared. A 1-byte unpaired switch keeps `{{ value }}` as its command; a wider ERD keeps the first-byte slice in its value template so `01`/`00` state matching still works.
- **Partial-write demotion** (`_demote_partial_write_entities`, run at the end of `_collect_ha_discovery_entries`). The bridge writes MQTT payloads verbatim — there is no read-modify-write. Controllable entities are grouped by target ERD (their own ERD, or the paired request ERD when `pair_role == 'status'`), and an entity stays writable only if all hold: its rendered command covers the target's full `get_erd_byte_size()`; it is the sole controllable writer of that ERD; a bit-field switch starts at payload bit 0 (`bit_start == 0`) and the target has exactly one non-reserved field, because a 1-byte `01`/`00` command sets the whole payload and would zero sibling bits or mis-address a bit elsewhere. An **unknown target ERD fails closed** — the write width cannot be verified, so the whole group is demoted. Buttons are excluded: their one-shot `1` payload decodes to a 0-byte write that clobbers nothing.
- **`command_bytes` accounting** (`_number_command_bytes`, `_bitfield_sub_value_template` callers). The demotion pass compares the *rendered* command width against the target ERD, not the declared field size. Unsigned scaled values can overflow the `%0{2*size}x` format (255 × 9 = `0x8f7` needs 3 hex chars in a `%02x` field); signed templates apply a modulo and never exceed it. `command_bytes` and `bit_start` are demotion inputs only — they are never emitted to the JSONL.
- **Request/status pairing** (`_find_paired_field`, `_collect_ha_discovery_entries`, `_handle_mixed_erd`). Pairing is per-field when the review block carries `paired_erd`/`pair_role` on a field, falling back to the ERD-level values — mixed-pairing ERDs are common, so an ERD-level-only model loses the pair. A field is matched to its partner by name after the `Request`/`Status` suffix is stripped, then by `(offset, size)`. A request ERD with no valid status counterpart is dropped entirely; a status ERD is dropped when its paired request is controllable (`switch`/`select`/`number`) and every non-reserved field is paired, because the request entity already carries both state and command. A paired request's value template and options come from the *status* field's offset, size, and enum values, so the entity shows the appliance's actual state rather than the value it was last written. Bit-field sub-entities of a paired request inherit the parent domain instead of collapsing to `binary_sensor`/`sensor`, which is what keeps them controllable.
- **Domain selection** (`auto_detect_ha_domain.infer_ha_domain`). Check order is enum → raw → bool → bit-field → string → numeric: the enum check must precede the bit-field check so descriptive enums are not flattened, and bool precedes bit-field. A binary enum becomes `switch`/`binary_sensor` only when its functional values are keyed exactly `0` and `1` (`_binary_enum_keyed_zero_one`) — those entities encode state as `00`/`01`, so an enum keyed 1=Open / 2=Closed cannot round-trip and stays `select`/`sensor`. `DESCRIPTIVE_COMMANDS` beats `BINARY_LABELS`: `go to agitate` / `go to spin` are in both sets and are descriptive commands, not toggles. A writable `bool` whose name contains `restart`, `factory reset`, `clear`, `reset`, or `cancel` is a `button`. Multi-bit fields: writable → `number`, read-only → `sensor`. A `binary_sensor` cannot display enum labels, so enum-typed sub-fields rendered there use ON/OFF.

### Detection rules

- **Device-class detection order** (`auto_detect_device_class.infer_device_class`). Unit-based detection runs before keyword detection so a strong unit signal is not overridden by a keyword false positive, and it still applies the exclusions table (`_check_exclusions`). Occupancy matches `occupied` in the *field name only* — matching the ERD name or description produces capability flags such as "Occupancy Present". Problem indicators (`fault`, `issue`, `error`, `alarm`, `limited`) match the combined field name + ERD name + description. Bit-fields get no device class (they are boolean indicators), and enum detection runs before the numeric-type gate because enum fields carry `field_type='enum'`.
- **Keyword matching width.** Keywords of 2 characters or fewer are skipped; 3–7 character keywords use word-boundary matching (`_word_bound`, which treats `_` as a separator) so `temp` does not match `attempts` and `timeout` does not match `timeouts`; longer keywords use plain substring matching.
- **`device_class='duration'` must never be assigned.** `infer_device_class` skips the `duration` keyword group even though `ha_constants.DEVICE_CLASS_KEYWORDS` defines it and `VALID_DEVICE_CLASSES` permits it. The removed comment cited an "AGENTS.md rule"; no such file exists in this repository, so this bullet is the rule's home.
- **Enum value classes.** 3 or more functional values (protocol markers excluded) → `enum`; a descriptive 2-value pair → `enum`; an on/off pair gets no device class because `binary_sensor`/`switch` handle it.
- **`apply_detection` write policy differs per detector.** `auto_detect_device_class.apply_detection` clears a device class left by an earlier run when the detector no longer matches, and always writes when it does — overwriting a hand-set value. `auto_detect_ha_domain.apply_detection` skips any entry that already has `ha_domain`. A manual `device_class` therefore survives through `post_process.py` re-applying `OVERRIDES` last in `run_pipeline.py`, not through the detector leaving it alone.

### Overrides

- **Override key forms** (`post_process.apply_overrides`). `erd_id:offset` is tried first and matches only the field at that byte offset, which makes overrides stable against upstream field renames; a bare `erd_id` applies to every field of the ERD and is safe only for single-field ERDs. `field_offset` must be a real `int` — `bool`, `float`, `str`, or `None` falls through to the bare key.
- **Overrides run last.** `auto_detect_scaling` clears `unit_of_measurement` and `scaling_factor` on non-numeric fields, so an override for such a field only survives because `post_process.py` runs after it in `run_pipeline.py`.
- **Generator-side overrides.** `value_template` replaces the auto-generated template (needed for ERDs whose value spans several fields, e.g. a u64 split across two u32s) and `force_classification` replaces whatever `_classify_erd_data` returned. Both are read from the review block in `_handle_single_erd` and `_collect_ha_discovery_entries`.
- **Scaling guard.** `_compute_number_range` clamps a non-positive `scaling_factor` to 1 because a zero scale divides by zero. `post_process.py` Rule 4 already repairs `scaling_factor == 0`, but only for `sensor`/`number` domains, so the generator keeps its own guard.

### Identity and naming

- **Field-name parsing** (`_leaf_field_name`, `_clean_field_name`, `_field_slug`). A purely numeric last dot-segment whose parent ends in an alphanumeric character is part of the value, not a namespace separator (`Air Purifier.PM2.5` → leaf `PM2.5`). Trailing array indices (`[0]`, `[1]`) are kept when parenthetical unit suffixes are stripped, because they are what make repeated sub-fields unique. Slugs cap at 64 characters: long enough to stay distinct within one ERD's sub-fields, short enough to keep unique_id segments reasonable.
- **unique_id collisions** (`_make_unique_field_id` inside `_collect_ha_discovery_entries`). Field ids must be unique within an ERD or Home Assistant unique_ids collide; the second and later occurrences get `_1`, `_2`.
- **Version-field classification** (`_version_field_role`, `_extract_board_prefix`, `_is_simple_version_erd`, `_group_multi_board_version_fields`, `_classify_erd_data`). Longer and non-critical keywords are matched first — `critical major` is a substring of `non-critical major`. The simple 4-byte version test must run before the byte-offset/bit-field classification, or a version ERD is split into four numbers. A `multi_board_version` ERD is only recognized when every non-reserved field is accounted for by a board group that has all four version components.
- **Field inheritance.** Non-numeric sub-fields never inherit the ERD-level unit or scaling factor, so an enum status field cannot show `gal/min` copied from a sibling. A field name that names a unit overrides the parent unit (`_infer_unit_from_field_name`; ERD `0x7705` carries one degF and one degC field under a degF parent).
- **Metadata filtering** (`collect()` inside `_collect_ha_discovery_entries`). Availability/allowability metadata is dropped — it is not actionable in Home Assistant — but real data must survive: `Allowed Setpoint`, `Minimum Allowed X`, and `Maximum Allowed X` are data fields, not metadata.

### Output format and inputs

- **Value templates decode hex payloads.** ERD state payloads on MQTT are hexadecimal byte strings, so a sub-field template slices `value[offset*2:(offset+size)*2]` (`_byte_subfield_value_template`). GEA sends multi-byte values most-significant byte first, so a float32 is decoded with `from_hex | unpack('>f')` — those Home Assistant template filters require HA 2023.4 or later. Signed integers get two's-complement extension in the template, and `_number_command_template` uses a modulo rather than a bitwise AND because Jinja2 has no `&`. String ERDs are plain ASCII on a hex payload: the template maps each byte through a printable-range lookup string indexed `byte - 0x20` (index 0 = space, index `0x5E` = `~`) and strips trailing `_` padding.
- **Select options.** `Request Consumed` (255) is excluded from options — it is a write-only protocol marker, never a user-visible state. An unmapped raw value maps to the literal `None`, which Home Assistant's MQTT select reports as "state unknown"; any other non-option string is logged as an error on every update, which is what happens to a `00` request ERD whose options only cover `01`/`02`. Read-only enum sensors fall back to `Unknown`, then to the raw first byte.
- **Entity validity gates** (`generate_ha_discovery_jsonl_by_category`, `_handle_single_erd`). A `select` with no options and a `number` whose min equals its max are skipped rather than emitted: both are broken in Home Assistant. A `device_class` is emitted only when valid for the domain (`_is_valid_device_class`); a domain absent from `VALID_DEVICE_CLASSES` has no restrictions.
- **Compact JSONL encoding.** Keys are abbreviated (`i`, `n`, `d`, `ds`, `u`, `dc`, `sc`, `sf`, `mn`, `mx`, `st`, `p`, `r`, `vt`, `ct`, `o`, `fi`, `m`, `pon`/`poff`/`son`/`soff`) and any field equal to its default is omitted, because the JSONL is zlib-compressed into `ha_discovery_data.h` and pays for itself in flash. The keys are read by the firmware's own line parser (`json_get_str()` calls in `ha_discovery_manager.cpp`), so adding, renaming, or re-typing a key is a cross-module change: the parser has to change in the same commit, and the roundtrip is covered by `test_compress_ha_discovery.py`.
- **Appliance-API input search paths** (`find_erd_definitions_json`, `fetch_erd_definitions_from_github`). Five locations are tried in order — the `lib/public-appliance-api-documentation` submodule, the ESPHome external-files cache under `$HOME`, the `/config/.esphome` cache used by the Home Assistant add-on, the `.esphome` build directory, and the parent-library `external_components` layout — deduplicated by resolved path, with the upstream GitHub `main` branch as the last resort. The last entry is byte-identical to the first, so it is a dedup no-op rather than a distinct layout.