# HA Discovery Entity Generation

What decides what a generated Home Assistant entity looks like: its unit, scale,
state class, domain, device class, and whether it is paired. The constraints below
are the durable ones — the pipeline sources carry no prose comments
([ADR-0001](../adr/0001-source-comments-banned.md)), so this page is where they
live. Breaking one silently changes which entities an appliance gets, or gives
Home Assistant an entity it rejects.

For how to run the pipeline, what each stage is for, and the override key/value
reference, see the [pipeline guide](../guides/pipeline.md). This page records only
the rules the code depends on. Which ERDs discovery may publish for is
[ADR-0008](../adr/0008-valid-erd-filter-borrows-one-array.md); a board that is
silent at one boot keeping its entities is
[ADR-0019](../adr/0019-silent-board-keeps-its-entities.md).

## Where each attribute is decided

|Attribute|Decided by|Section|
|---|---|---|
|`unit_of_measurement`, `scaling_factor`|`auto_detect_scaling.detect_unit_and_scaling`, `infer_unit_from_name`|[Unit and scaling](#unit-and-scaling-detection)|
|`state_class`|`auto_detect_state_class.infer_state_class`|[State class](#state-class-detection)|
|`paired_erd`, `pair_role`|`auto_detect_pairings.find_erd_pairs`, `match_fields`|[Pairing](#requeststatus-pairing-detection)|
|manual corrections|`post_process.OVERRIDES`, `apply_overrides`|[Overrides](#overrides)|
|cross-field repair|`post_process.apply_post_processing`|[Post-validation rules](#post-validation-rules)|
|domain, device class|`auto_detect_ha_domain.infer_ha_domain`, `auto_detect_device_class.infer_device_class`|[pipeline guide § Detection rules](../guides/pipeline.md)|
|entity payload shape|`generate_ha_discovery.py`|[pipeline guide § Output format and inputs](../guides/pipeline.md)|
|writability (command topic)|`generate_ha_discovery._demote_partial_write_entities`|[No partial writes](#no-partial-writes)|

All of them read and write the `review` block of
`scripts/ha_discovery/appliance_api_erd_definitions_processed.json`, which is the
pipeline's only shared state.

## Run order is load-bearing

`run_pipeline.py` runs the stages in a fixed order: flatten → `auto_detect_pairings`
→ `auto_detect_ha_domain` → `auto_detect_device_class` → `auto_detect_scaling` →
`auto_detect_state_class` → `post_process` → `generate_ha_discovery` →
`compress_ha_discovery`.

- **Overrides are last, and that is what makes them work.** Every detector either
  overwrites what it detects or clears values it no longer detects, so a hand-set
  value survives a rerun only because `post_process.py` re-applies `OVERRIDES`
  after all of them. Inside `post_process.main()`, `apply_post_processing()` runs
  before `apply_overrides()`, so an override also wins over the repair rules.
- **Pairings are re-computed from scratch every run.**
  `auto_detect_pairings.apply_pairings()` resets `paired_erd` and `pair_role` to
  `None` on every entry before it searches. A manual pair therefore has to live in
  `OVERRIDES`; writing it into the processed JSON by hand is erased by the next
  run.
- **The processed JSON is written atomically.** `run_pipeline_script()` and the
  flatten step write to a `tempfile.mkstemp` file in the *same* directory as the
  target (prefix `.tmp_appliance_api_erd_definitions_processed`, suffix `.json`)
  and then `shutil.move` it over the target; on any exception the temp file is
  unlinked and the error re-raised. A failed stage therefore never truncates the
  processed JSON that the next stage reads, and a rerun starts from the last good
  state — that is what makes the pipeline idempotent. Steps 8 and 9 write their
  own outputs directly and are not temp-renamed.

## Unit and scaling detection

`auto_detect_scaling.detect_unit_and_scaling(field_name, field_bits=None)` consults
three sources, in this precedence:

1. **The trailing parenthetical group**, lowercased, looked up in `UNIT_MAP`
   (`PAREN_GROUPS` takes the *last* group). This is the most reliable unit signal,
   which is why it is consulted first.
2. **`INLINE_UNIT_SCALING` patterns** matched against the whole field name. An
   inline pattern that yields *both* a unit and a non-1 scale **overrides** the
   parenthetical result, because it sees the full name context; an inline pattern
   may also add a scale when the parenthetical already supplied the unit.
3. **`infer_unit_from_name` keyword fallback**, and only when the parenthetical
   group did not match at all.

Constraints that hold this together:

- **`(None, None)` entries in `UNIT_MAP` are suppression entries, not no-ops.**
  Entries such as `'actual'`, `'desired'`, `'compressor'`, `'0-255'`,
  `'least significant 32 bits'` are metadata, not units. Matching one sets
  `paren_matched`, which suppresses the `infer_unit_from_name` fallback — delete
  the entry and the detector starts inferring a unit from the field name. They do
  **not** suppress the inline patterns, which run regardless.
- **`UNIT_MAP` value semantics.** Keys are lowercase (the lookup lowercases the
  suffix). A `None` in a value means *do not set that attribute*, not *clear it*:
  `'speed * 10': (None, 10)` supplies a scale and no unit.
- **Compound units are skipped, on both paths.** `liter/minute`,
  `GallonsX100/Minute` are flow rates with no single Home Assistant
  `unit_of_measurement`. In `infer_unit_from_name` any `/` in the name returns
  `(None, None)`. In `detect_unit_and_scaling` the compound test is
  `has_compound = '/' in name_without_parens and not re.search(r'/[A-Z][a-z]+ ', name_without_parens)`:
  only the trailing parenthetical is stripped before the test, so `(1/100 Gallons)`
  still reads as gallons, and a menu separator (`Biscuit/Dinner Roll`) is not
  mistaken for a compound unit. The skip is additionally bypassed when the name
  contains `min/max`.
- **Pattern order in `INLINE_UNIT_SCALING` is load-bearing**: more specific
  patterns first. Within a pattern, `.*?` is non-greedy so text may sit between
  the unit keyword and the scale, and the alternation is `(100|10)` so `100` is
  tried before `10`. Each entry is `(regex, resolver)` and the regex's first
  capture group is the scaling factor — a new pattern that captures something else
  in group 1 resolves garbage.
- **Two guards carry their whole pattern:** `(?!ary)` after `seconds?` stops
  `Secondary` from matching the seconds pattern, and `(?<!milli)` before `liter`
  stops `Milliliter` from matching the liter pattern. The reversed-order entries
  (`x 10 (ounces)`, `x 100 (volts)`) exist because the scale can precede the
  parenthetical unit; they are not duplicates of the forward patterns.
- **Substring traps in `infer_unit_from_name`.** `milliliter` is tested before
  `liter`, `gallon` before `liter`, and `grams` uses `\bgrams?\b`, because the
  checks are substring tests and the shorter keyword is inside the longer one.
  `watt second` is excluded from the watt rule — watt-seconds is energy, not
  power. `average`/`load average`/`time of day` are excluded from the time rules,
  `ground gram` from the gram rule, `update period` from the humidity rule.
- **Bit width plays no part in unit or scaling detection.**
  `detect_unit_and_scaling` accepts `field_bits` and never reads it; the caller at
  `apply_detection` still passes `entry.get('field_bits')`. There is no "skip
  1-bit boolean flags" behavior anywhere in this script or elsewhere in the
  pipeline — do not assume bit-width gating exists, and do not add it by
  re-reading a comment that claims it.
- **Only numeric fields are considered**: `field_type` must be one of `u8 u16 u32
  i8 i16 i32`. Non-numeric entries have any `unit_of_measurement` and
  `scaling_factor` *cleared*, because the processed JSON carries values from
  previous runs — that is how an enum field that inherited a unit from a sibling
  override gets cleaned.
- **The write policy is asymmetric.** A detected unit is always written,
  overwriting a stale or wrong value. A detected scale is written only when the
  `scaling_factor` **key is absent** from the review block, so an explicit
  `"scaling_factor": None` (e.g. the `0x7a02` override) is respected. When neither
  a unit nor a scale is detected, the stale unit is cleared; when one is detected,
  the detector writes. A scale is also recovered from a bare `x <N>` in the field
  name, then in the ERD description.

## State-class detection

`auto_detect_state_class.infer_state_class` applies Home Assistant's long-term
statistics rules:

- `state_class` is valid **only for `sensor`** entities.
- Non-numeric device classes (`enum`, `timestamp`, `date`, `uptime`) output text
  strings; a `state_class` on them makes Home Assistant reject the sensor for LTS.
  The detector keeps its own local copy of that set rather than importing
  `ha_constants.SENSOR_NON_NUMERIC_DEVICE_CLASSES`; the two currently agree, and
  they must keep agreeing.
- The three classes mean what Home Assistant means: `measurement` is an
  instantaneous value, `total` is a cumulative counter that **can go up and down**,
  `total_increasing` is a counter that **only increases**.
- With a device class: `cumulative`/`total`/`consumption`/`usage`/`since clear` →
  `total`, but only when the device class is `energy`, `gas`, `water` or `volume`,
  and never when the name contains `average` — an average is instantaneous, not a
  cumulative counter. `count`/`number of`/`runtime`/`uptime` → `total_increasing`.
  `cycle` → `total_increasing` only via `_word_bound` (which treats `_` as a
  separator), and only for non-`temperature` device classes, because `MyCycle` is
  a GE product feature name, not a cycle count. Otherwise `measurement`, and only
  when `SENSOR_DEVICE_CLASS_STATE_CLASSES` allows `measurement` for that device
  class; cumulative/total names are excluded from it.
- Without a device class: a counter/time/level/position keyword in the field name
  **plus the ERD description** yields `measurement` at confidence 0.7. This is
  deliberate — the unit is unknown but the name says the value is measurable.
- `apply_detection` writes only when `state_class` is not already set, so a manual
  state class survives re-detection; the confidence is recorded in
  `review['_sc_confidence']`.

## Request/status pairing detection

`auto_detect_pairings` pairs a request ERD with its status ERD by name, then pairs
their fields structurally.

- **Name normalization order matters.** `normalize_field_name` strips the
  parenthetical hint **first** (`Status (0-100)` → `status`), then the
  `Request`/`Status` suffix, then takes the last dot-qualified segment, then
  collapses whitespace. Reordering the first two leaves the suffix attached and
  the pair is missed. `strip_request_status` also handles the compound forms
  (`… - Requested/Desired`, `… - Status/Actual`) and the trailing-number form
  (`Request 0`), both of which occur in the upstream API data.
- **Field matching is two-phase.** Phase 1 requires the same normalized name *and*
  the same field type, byte offset, size, bit offset (when either side is a
  bit-field) and array index (when either side has one). Phase 2 is a fuzzy name
  match (`field_match_score` ≥ 50: 100 exact, 70 containment, 60/50 by stemmed
  word-overlap ratio, with at least one common word longer than one character).
- **One match per request field, one pair per ERD.** Phase 1 `break`s after the
  first status field, and `find_erd_pairs` `break`s after the first candidate
  partner — an ERD is in at most one pair. Nothing else in the module states
  that; removing either `break` produces multi-paired ERDs and duplicate entities.
- **The second pass** pairs an unpaired request ERD with a *bare* counterpart
  (`Requested X` ↔ `X`, `X Request` ↔ `X`) using `bare_by_base`, which is built
  only from ERDs whose names contain neither keyword.

## Overrides

`post_process.OVERRIDES` is the only place a hand-set entity attribute is durable.

- **Key forms.** `erd_id:offset` matches only the field at that byte offset and is
  stable against upstream field renames; a bare `erd_id` applies to *every* field
  of the ERD and is safe only for single-field ERDs. `apply_overrides` tries the
  exact `erd_id:offset` key first and falls back to the bare key.
- **`field_offset` must be a real `int`.** `bool` is an `int` in Python and is
  explicitly excluded; `str`, `float`, `None` or `bool` offsets fall through to
  the bare-key path only.
- **One ERD can hold only one ERD-level pair**, which is why the setpoint-limit
  overrides (`0x770d`/`0x770f` ↔ `0x7708:0`/`0x7708:2`,
  `0x7711`/`0x7713` ↔ `0x770b:0`/`0x770b:2`) carry the status side as
  `erd_id:offset` entries: a single ERD-level `paired_erd` cannot name two
  different request ERDs.
- Only values that differ from the current review value are written, and the
  return value counts those writes.

The per-ERD evidence behind the shipped overrides, in the classes that recur:

|Class|ERDs|Why the detector is wrong|
|---|---|---|
|Read-only positions forced to `sensor`|`0x7902`|A 4-way valve position is read-only; `select` would advertise a command topic the appliance never accepts|
|Binary enums not keyed `0`/`1`|`0x0004` (1/2), `0x209d` (0/255), `0x4223` (1/2), `0x7968:12` (0/255)|`switch`/`binary_sensor` encode state as the literal `00`/`01`, which cannot represent these values; `select`/`sensor` map the labels to the real values|
|Cumulative counters that reset|`0x4101`, `0x4105`|They reset to 0, so `total_increasing` is wrong; `measurement` is right|
|Signals keyed 0/1|`0x4102`|A satisfied flag, not a counter|
|Unit the API sends directly|`0x7464:*`, `0x7465` (CFM)|No conversion: the API already sends CFM|
|Wire scale the name does not state|`0x4065` (A ×10), `0xd030` (×1000), `0x7134`/`0x7135` (% ×100)|The scale is a wire fact, not a name pattern|
|HA device-class restrictions|`0x800d`|The `water` device class only allows `total`|
|Value split across fields|`0x404c:0`|Anode depleted mass is a u64 split into MSB+LSB u32s, sent in micrograms: `value_template` recombines and scales to grams|
|Units the name never states|fan `rpm`, EEV/valve `steps`, anode `h`, usage-period `min` (`0xd014`/`0xd02c`/`0xd017`/`0xd02d`/`0xd01a`/`0xd02e`/`0xd01d`/`0xd02f`, `0x5b27`/`0x5b28`), tub level `mmH₂O`, inlet flow `gal/min` ×10000|Nothing in the field name to detect; the usage-period ERDs need no manual pairing — those pairs are auto-detected|
|State class added for graphing|`0x400a`–`0x400c`|Mixing-valve step positions get `state_class: measurement` so Home Assistant can plot them|
|Manual pairs|`0x770d`/`0x770f`/`0x7711`/`0x7713` ↔ `0x7708`/`0x770b`|See the one-pair-per-ERD rule above|

`0x7a02` is the inverse case: `{"scaling_factor": None}` *removes* a scale the
detector would otherwise infer.

## Post-validation rules

`post_process.apply_post_processing` repairs cross-field inconsistencies the
detectors can produce. Rules 1 and 2 restate what the domain allows; rules 3 and 4
enforce Home Assistant semantics:

1. `binary_sensor`/`switch` never carry `unit_of_measurement`.
2. A `number` device class must be valid for the `number` domain
   (`ha_constants.VALID_DEVICE_CLASSES['number']`), otherwise it is cleared.
3. A sensor with a device class and no state class gets `measurement` — but only
   for device classes that actually support it. Non-numeric classes (`enum`,
   `timestamp`, `date`, `uptime`) output text; some numeric classes allow only
   `total`/`total_increasing` (`energy`, `gas`, `water`, `volume`,
   `reactive_energy`, `monetary`) or only `measurement_angle` (`wind_direction`).
4. `scaling_factor == 0` is repaired to 1, because a zero scale zeroes every value.
   Only `sensor`/`number` are touched — the domains where scaling is applied.

These run before `apply_overrides`, so an override that contradicts a rule wins.

## No partial writes

The bridge writes the MQTT payload it receives **verbatim** to the ERD — there is
no read-modify-write — so a command that fills only part of an ERD payload
clobbers or mis-addresses whatever occupies the rest.
`generate_ha_discovery._demote_partial_write_entities()` (run at the end of
`_collect_ha_discovery_entries`, after every classification handler) strips the
command from those entities.

**What is checked:** `switch`, `number` and `select` entries that have a command
template or a paired ERD — anything else already gets no command topic. `button`
is not checked: its one-shot payload is not a state write.

**Target ERD:** the entity's own ERD, or its paired request ERD when
`pair_role == 'status'` — a status entity writes through the request ERD, so its
command width is compared against that ERD, not its own.

An entity stays writable only if all of these hold:

- its rendered command width equals the target's full `get_erd_byte_size()` — the
  whole payload, not the field's own size;
- it is the only controllable writer of that target. Two writers of one ERD are
  both demoted, even when each is full-width;
- a bit-field switch starts at payload bit 0 **and** is the sole non-reserved
  field of the target (`_is_reserved_field`: the name contains `reserved` or
  `padding`). A 1-byte `01`/`00` command sets the entire payload, so a bit
  elsewhere is mis-addressed and a read-only sibling bit is zeroed;
- the target ERD is present in the definitions. An unknown target cannot be
  verified, so the pass **fails closed** and demotes the whole group; defaulting
  the target size to 1 would keep exactly the writers this rule exists to remove.

**What demotion does to the generated config:** `switch` → `binary_sensor`,
`number`/`select` → `sensor`; `command_template` and the option list are cleared
and `command_bytes` is reset to 0. The value template and state payloads are kept,
so the entity stays readable with its unit, scaling and state class intact. The
pass prints the number of demoted entities and ERDs to stderr.

**Command width is the rendered width, not the declared field size.**
`_number_command_bytes` takes the worst case over the entity's range: an unsigned
scaled command renders `round(value * scaling_factor)` in `%0{2*size}x`, and a
value near the range limit can need more hex chars than the field width
(255 × 9 = `0x8f7` needs 3 chars in a `%02x` field). Signed templates apply a
modulo and cannot overflow it. `command_bytes` and `bit_start` are demotion inputs
only — they are never emitted to the JSONL.

**The gate that keeps it true:** `scripts/test_ha_discovery.py::TestNoPartialWrites`
runs against the *generated* entities, not fixtures: every entity with a command
topic must have a deterministic rendered width (`_rendered_bytes` reads the
`%0Nx` format, the literal `{{ value }}`, or a literal hex payload), that width
must equal the target ERD's `ds`, and no target ERD may have two writers. A new
ERD definition or override that reintroduces a partial writer fails CI.
`TestPartialWriteDemotion` pins the unit cases: two writers of one target; a
2-byte enum in a 4-byte ERD (the `0xd023` shape — enum plus reserved padding); a
bit-field switch at a non-zero bit, at bit 0 with a read-only sibling bit, and at
bit 0 as the sole non-reserved field; a `status` writer targeting its paired
request ERD; the unknown-target fail-closed case (`cb=1` is what distinguishes it
from the old fail-open default); and an end-to-end `_handle_byte_offset_erd` case
(`0x2164` Cycle Pricing — two u16 sub-fields in a 4-byte ERD, both demoted).

Command-topic eligibility — the earlier demotion of *unpaired* `switch`/`number`
sub-fields of a multi-field ERD — is recorded in the
[pipeline guide § Entity shape and writability](../guides/pipeline.md).

## The Home Assistant validation tables

`scripts/ha_discovery/pipeline/ha_constants.py` is the single source of truth for
the Home Assistant vocabulary; the detectors and generators import it instead of
holding local copies. The device-class and unit tables mirror Home Assistant's own
`DEVICE_CLASS_UNITS`, `DEVICE_CLASS_STATE_CLASSES` and `STATE_CLASS_UNITS`
constants, so they are updated by tracking upstream, not by local preference.

|Table|Consumed by|
|---|---|
|`VALID_DEVICE_CLASSES`|`post_process.apply_post_processing` rule 2; `generate_ha_discovery._is_valid_device_class`; also imported by `auto_detect_ha_domain`|
|`SENSOR_DEVICE_CLASS_STATE_CLASSES`|`auto_detect_state_class.infer_state_class`; `post_process` rule 3|
|`SENSOR_NON_NUMERIC_DEVICE_CLASSES`|`post_process` rule 3; `auto_detect_device_class`, `auto_detect_ha_domain`|
|`BINARY_SENSOR_DEVICE_CLASSES`|`auto_detect_ha_domain`|
|`DEVICE_CLASS_KEYWORDS`, `DEVICE_CLASS_EXCLUSIONS`|`auto_detect_device_class`, `auto_detect_ha_domain`|
|`UNIT_KEYWORD_MAP`|imported by `auto_detect_device_class`, unused there today|
|`CATEGORIES`|`generate_ha_discovery.get_category`, `generate_ha_discovery_jsonl_by_category`, and the import-time `_unknown_categories` check|
|`CATEGORIES_LIST`|`compress_ha_discovery.generate_header` / `generate_cpp`; `scripts/test_ha_discovery.py`|
|`PROTOCOL_MARKERS`|`auto_detect_ha_domain` (label filtering), re-exported to `auto_detect_device_class` for enum-value counting|

Details that are easy to "clean up" into a bug:

- **`SENSOR_DEVICE_CLASS_UNITS["monetary"]` is an empty `set()` on purpose.** Any
  ISO 4217 code is a valid monetary unit, so no unit list can be validated; the
  empty set means *skip unit validation*, not *no units allowed*.
- **`measurement_angle` is in the state-class vocabulary.** `wind_direction`
  accepts only `measurement_angle`, and `area`, `data_size`, `distance`,
  `duration`, `precipitation`, `weight` carry all four classes because upstream
  builds them from the full `SensorStateClass` enum.
- **`NUMBER_DEVICE_CLASS_UNITS` is a deliberate independent copy** of
  `SENSOR_DEVICE_CLASS_UNITS` (`dict(...)` at import), not a shared reference: the
  two domains drift apart on purpose.
- **`UNIT_IMPLYING_DEVICE_CLASSES` is derived, not curated**: it is exactly the
  `SENSOR_DEVICE_CLASS_UNITS` entries that do not allow `None` as a unit.
- **Currently unreferenced** (verified 2026-11 by searching every script and test
  in the repo): `UNIT_IMPLYING_DEVICE_CLASSES`, `VALID_STATE_CLASSES`,
  `STATE_CLASS_UNITS`, `NUMBER_DEVICE_CLASS_UNITS`, `MEASUREMENT_DEVICE_CLASSES`,
  `TOTAL_STATE_CLASSES`, `BINARY_SENSOR_KEYWORDS`, `NUMERIC_TYPES`, and
  `SENSOR_DEVICE_CLASS_UNITS` itself (only its `NUMBER_DEVICE_CLASS_UNITS` copy
  refers to it). They are upstream mirrors kept for validation work; editing one
  changes no generated entity today.

## ERD category ranges

`ha_constants.CATEGORIES` is the authoritative ERD-range → category mapping. The
category token is the JSONL stem, the generated `ha_discovery_categories[]` name
the firmware `strcmp`s, and the key in the appliance-type mapping
([appliance type → category](./appliance-type-categories.md)).

|Category|ERD range|
|---|---|
|`common`|0x0000–0x0FFF|
|`refrigeration`|0x1000–0x1FFF|
|`laundry`|0x2000–0x2FFF|
|`dishwasher`|0x3000–0x3FFF|
|`waterheater`|0x4000–0x4FFF|
|`range`|0x5000–0x5FFF|
|`airconditioning`|0x7000–0x7FFF|
|`waterfilter`|0x8000–0x8FFF|
|`smallappliance`|0x9000–0x9FFF|
|`energy`|0xD000–0xDFFF|

- The ranges are authoritative; `CATEGORIES_LIST` is derived from them
  (`list(CATEGORIES.keys())`) for consumers that only need the names.
- The gaps (0x6000–0x6FFF, 0xA000–0xCFFF, 0xE000 and above) are unassigned.
  `get_category()` returns `None` there and
  `generate_ha_discovery_jsonl_by_category()` warns to stderr with the offending
  ERD ids instead of dropping them silently.
- `PROTOCOL_MARKERS` (`Request Consumed`, `Request processed`, `Consumed`) are
  protocol markers, not enum values: they are excluded from the enum-value counts
  that decide whether a field is an enum, and from `select` options.
