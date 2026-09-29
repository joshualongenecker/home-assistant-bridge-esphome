#!/usr/bin/env python3
"""Post-processing: fix cross-field consistency issues after auto-detection.

Applies these rules:
1. Clear unit_of_measurement for binary_sensor/switch (they shouldn't have units).
2. Clear device_class for number domain (except temperature).
3. Add state_class=measurement for sensor with device_class but no state_class.
4. Fix scaling_factor=0 to 1 for sensor/number domains (semantically invalid).

Run this after all auto-detection scripts for a clean state.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pipeline_utils import SCRIPT_DIR, REPO_ROOT, load_json
from ha_constants import SENSOR_DEVICE_CLASS_STATE_CLASSES, SENSOR_NON_NUMERIC_DEVICE_CLASSES, VALID_DEVICE_CLASSES



# Module-level overrides — can be overridden by tests for unit testing.
OVERRIDES = {
    # --- Read-only: force ha_domain=sensor ---
    "0x7100": {"ha_domain": "sensor"},
    "0x7101": {"ha_domain": "sensor"},
    "0x7102": {"ha_domain": "sensor"},
    "0x7103": {"ha_domain": "sensor"},
    "0x7108": {"ha_domain": "sensor"},
    "0x710a": {"ha_domain": "sensor"},
    "0x7130": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
    "0x7131": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
    "0x7132": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
    "0x7133": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
    "0x7601": {"ha_domain": "sensor"},
    "0x7512": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
    "0x7513": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
    "0x7514": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
    "0x7515": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
    "0x7104": {"ha_domain": "sensor"},
    "0x7114": {"ha_domain": "sensor"},
    "0x7115": {"ha_domain": "sensor"},
    "0x4026": {"ha_domain": "sensor"},
    "0x7907": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
    "0x7938": {"ha_domain": "sensor"},
    "0x710b": {"ha_domain": "sensor"},
    # --- Fan speed: add rpm unit ---
    "0x7136": {"unit_of_measurement": "rpm"},
    "0x7137": {"unit_of_measurement": "rpm"},
    "0x5b13": {"unit_of_measurement": "rpm"},
    # --- Fan PWM: add % unit and scaling ---
    "0x7134": {"unit_of_measurement": "%", "scaling_factor": 100},
    "0x7135": {"unit_of_measurement": "%", "scaling_factor": 100},
    # --- EEV positions: add steps unit ---
    "0x7518": {"unit_of_measurement": "steps"},
    # --- WAC Ambient: remove incorrect scaling ---
    "0x7a02": {"scaling_factor": None},
    # --- Appliance Cumulative Energy: add scaling ---
    "0xd030": {"scaling_factor": 1000},
    # --- Water Softener Daily Usage: water device_class only allows total ---
    "0x800d": {"state_class": "total"},
    # --- Anode depleted mass: combine MSB+LSB u32 fields into single u64,
    #     scale from micrograms to grams (10^-6) ---
    "0x404c:0": {
        "ha_domain": "sensor",
        "device_class": "weight",
        "unit_of_measurement": "g",
        "state_class": "measurement",
        "force_classification": "single",
        "value_template": "{{ ((value[0:8] | int(base=16)) * 2**32 + (value[8:16] | int(base=16))) / 1000000 | round(3) }}",
    },
    # --- Usage update period pairs: add min unit (pairing is auto-detected) ---
    "0xd014": {"unit_of_measurement": "min"},
    "0xd02c": {"unit_of_measurement": "min"},
    "0xd017": {"unit_of_measurement": "min"},
    "0xd02d": {"unit_of_measurement": "min"},
    "0xd01a": {"unit_of_measurement": "min"},
    "0xd02e": {"unit_of_measurement": "min"},
    "0xd01d": {"unit_of_measurement": "min"},
    "0xd02f": {"unit_of_measurement": "min"},
    "0x5b27": {"unit_of_measurement": "min"},
    "0x5b28": {"unit_of_measurement": "min"},
    # --- Mixing valve positions: add steps unit and state_class for plotting ---
    "0x400a": {"unit_of_measurement": "steps", "state_class": "measurement"},
    "0x400b": {"unit_of_measurement": "steps", "state_class": "measurement"},
    "0x400c": {"unit_of_measurement": "steps", "state_class": "measurement"},
    # --- Anode hours of service: add hours unit ---
    "0x404d": {"unit_of_measurement": "h", "state_class": "total"},
    # --- Inlet Flow Rate: GPM with x10000 scaling (first field only, offset 0) ---
    "0x3015:0": {"unit_of_measurement": "gal/min", "scaling_factor": 10000, "field_name": "Inlet Flow Rate"},
    # --- Heating cycle satisfied: signal (0/1), not a counter ---
    "0x4102": {"state_class": "measurement"},
    # --- Measured current: Amps with x10 scaling ---
    "0x4065": {"device_class": "current", "unit_of_measurement": "A"},
    # --- Missed flow off counter: resets to 0, so measurement ---
    "0x4101": {"state_class": "measurement"},
    # --- Missed flow off fault counter: resets to 0, so measurement ---
    "0x4105": {"state_class": "measurement"},
    # --- Number of resets: cumulative counter ---
    "0x40c0": {"state_class": "total_increasing"},
    # --- Input Current Limiting: Amps on Min/Max/Step fields ---
    "0x7470:2": {"device_class": "current", "unit_of_measurement": "A"},
    "0x7470:4": {"device_class": "current", "unit_of_measurement": "A"},
    "0x7470:6": {"device_class": "current", "unit_of_measurement": "A"},
    # --- Make-up Air Fan CFM: volumetric flow (API sends CFM, no conversion) ---
    "0x7464:0": {"unit_of_measurement": "CFM"},
    "0x7464:2": {"unit_of_measurement": "CFM"},
    "0x7464:4": {"unit_of_measurement": "CFM"},
    "0x7464:6": {"unit_of_measurement": "CFM"},
    # --- Make-up Air Fan CFM Request: paired with 0x7464 status ---
    "0x7465": {"unit_of_measurement": "CFM"},
    # --- Cycle counts: cumulative counters ---
    "0x3009:0": {"state_class": "total_increasing"},
    "0x3009:2": {"state_class": "total_increasing"},
    "0x3009:4": {"state_class": "total_increasing"},
    "0x3209:0": {"state_class": "total_increasing"},
    "0x3209:2": {"state_class": "total_increasing"},
    "0x3209:4": {"state_class": "total_increasing"},
    # --- Dispense counts: cumulative counters ---
    "0x2033": {"state_class": "total_increasing"},
    "0x2034": {"state_class": "total_increasing"},
    "0x2035": {"state_class": "total_increasing"},
    "0x2036": {"state_class": "total_increasing"},
    "0x203a": {"state_class": "total_increasing"},
    # --- Door counts: cumulative counters ---
    "0x3006": {"state_class": "total_increasing"},
    "0x3406": {"state_class": "total_increasing"},
    # --- Reset count: cumulative counter ---
    "0x0700": {"state_class": "total_increasing"},
    # --- Tub Water Level: mmH2O measurement ---
    "0x3084": {"unit_of_measurement": "mmH\u2082O", "state_class": "measurement"},
    # --- 4 Way Valve Position: read-only sensor, not select ---
    "0x7902": {"ha_domain": "sensor"},
    # --- Binary enums not keyed 0/1: switch/binary_sensor encode state as
    #     '00'/'01', which can't represent these values. Use select/sensor
    #     so the enum labels map to the real values. ---
    "0x0004": {"ha_domain": "select"},    # 1=UI Locked, 2=UI Not Locked
    "0x209d": {"ha_domain": "select"},    # 0=Start command, 255=Normal State
    "0x4223": {"ha_domain": "select"},    # 1=Open, 2=Closed
    "0x7968:12": {"ha_domain": "sensor"}, # 0=Inactive, 255=Active
    # --- Setpoint limit requests: pair request ERD with allowed setpoint ERD ---
    # Each request ERD (single field) pairs with the matching field in the
    # allowed setpoint ERD (multi-field). Per-field overrides handle the
    # status side since one ERD can't have two different ERD-level pairs.
    "0x770d": {"paired_erd": "0x7708", "pair_role": "request"},
    "0x7708:0": {"paired_erd": "0x770d", "pair_role": "status"},
    "0x770f": {"paired_erd": "0x7708", "pair_role": "request"},
    "0x7708:2": {"paired_erd": "0x770f", "pair_role": "status"},
    "0x7711": {"paired_erd": "0x770b", "pair_role": "request"},
    "0x770b:0": {"paired_erd": "0x7711", "pair_role": "status"},
    "0x7713": {"paired_erd": "0x770b", "pair_role": "request"},
    "0x770b:2": {"paired_erd": "0x7713", "pair_role": "status"},
}


def apply_overrides(entries, overrides=None):
    """Reapply documented overrides that auto-detection scripts may have reset.

    Override keys use the format ``erd_id`` or ``erd_id:offset`` where
    ``offset`` is the byte offset of the target field within the ERD.
    This makes overrides stable against field-name changes.

    * ``"0x7130"`` — applies to all fields of ERD 0x7130 (safe for single-field ERDs).
    * ``"0x3015:0"`` — applies only to the field at byte offset 0 of ERD 0x3015.

    Valid override value keys:

    * ``ha_domain`` — override the Home Assistant domain.
    * ``device_class`` — override the device class.
    * ``unit_of_measurement`` — override the unit string.
    * ``scaling_factor`` — override the scaling factor (``None`` to remove).
    * ``state_class`` — override the state class (``measurement``, ``total``, ``total_increasing``).
    * ``field_name`` — override the display name.
    * ``paired_erd`` — manually pair a request/status ERD.
    * ``pair_role`` — role in a pair (``request`` or ``status``).
    * ``force_classification`` — force a classification strategy.
    * ``value_template`` — custom Jinja2 template for value processing.

    Note: auto_detect_scaling clears unit_of_measurement and scaling_factor
    on non-numeric fields (enum, string). If you ever need to override a
    non-numeric field, the override must be reapplied in post_process
    (after auto_detect_scaling runs) to survive the cleanup.

    Args:
        entries: list of entry dicts with erd_id, field_offset, review.
        overrides: dict of override rules; defaults to module-level OVERRIDES.

    Returns the number of override values applied.
    """
    if overrides is None:
        overrides = OVERRIDES

    applied = 0
    for entry in entries:
        erd_id = entry.get("erd_id", "").lower()
        field_offset = entry.get("field_offset")

        # Guard: field_offset must be int (not bool) for offset-based matching.
        # Non-int values (string, None, bool, float) fall through to bare erd_id only.
        if not isinstance(field_offset, int) or isinstance(field_offset, bool):
            field_offset = None

        # Try exact key match first (erd_id:offset).
        if field_offset is not None:
            key = f"{erd_id}:{field_offset}"
            if key in overrides:
                override = overrides[key]
                review = entry.setdefault("review", {})
                for k, val in override.items():
                    if review.get(k) != val:
                        review[k] = val
                        applied += 1
                continue

        # Fallback: bare erd_id applies to all fields (safe for single-field ERDs).
        if erd_id not in overrides:
            continue
        override = overrides[erd_id]
        review = entry.setdefault("review", {})
        for k, val in override.items():
            if review.get(k) != val:
                review[k] = val
                applied += 1

    return applied


def apply_post_processing(entries):
    """Apply post-processing rules to all entries."""
    cleared_unit = 0
    cleared_dc = 0
    added_sc = 0
    fixed_scaling = 0

    for entry in entries:
        review = entry.setdefault('review', {})
        ha_domain = review.get('ha_domain')
        device_class = review.get('device_class')
        state_class = review.get('state_class')

        # Rule 1: binary_sensor/switch should not have units
        if ha_domain in ('binary_sensor', 'switch') and review.get('unit_of_measurement'):
            review['unit_of_measurement'] = None
            cleared_unit += 1

        # Rule 2: number domain device_class must be valid for number domain
        if ha_domain == 'number' and device_class:
            if device_class not in VALID_DEVICE_CLASSES.get('number', set()):
                review['device_class'] = None
                cleared_dc += 1

        # Rule 3: sensor with device_class should have state_class, but only
        # for device classes that actually support 'measurement'.
        # Non-numeric (enum, timestamp, date, uptime) output text strings.
        # Some numeric classes only allow 'total'/'total_increasing' (energy, gas,
        # water, volume, reactive_energy, monetary) or 'measurement_angle' (wind_direction).
        if (ha_domain == 'sensor' and device_class
                and not state_class
                and device_class not in SENSOR_NON_NUMERIC_DEVICE_CLASSES):
            valid_states = SENSOR_DEVICE_CLASS_STATE_CLASSES.get(device_class, set())
            if 'measurement' in valid_states:
                review['state_class'] = 'measurement'
                added_sc += 1

        # Rule 4: scaling_factor=0 is semantically invalid (would zero all values).
        # Only override for sensor/number domains where scaling matters.
        if ha_domain in ('sensor', 'number') and review.get('scaling_factor') == 0:
            review['scaling_factor'] = 1
            fixed_scaling += 1

    return cleared_unit, cleared_dc, added_sc, fixed_scaling



def main():
    parser = argparse.ArgumentParser(
        description='Post-process review fields for cross-field consistency.'
    )
    parser.add_argument(
        '--input',
        default=os.path.join(SCRIPT_DIR, '..', 'appliance_api_erd_definitions_processed.json'),
        help='Input processed JSON file',
    )
    parser.add_argument(
        '--output',
        default=None,
        help='Output processed JSON file (default: overwrite input)',
    )
    args = parser.parse_args()

    entries = load_json(args.input)
    cleared_unit, cleared_dc, added_sc, fixed_scaling = apply_post_processing(entries)
    n_overrides = apply_overrides(entries)

    print(f"Cleared {cleared_unit} unit_of_measurement values for binary_sensor/switch")
    print(f"Cleared {cleared_dc} device_class values for non-temperature number")
    print(f"Added {added_sc} state_class=measurement for sensors with device_class")
    print(f"Fixed {fixed_scaling} scaling_factor=0 values to 1")
    print(f"Reapplied {n_overrides} override fields")

    if args.output:
        with open(args.output, 'w', encoding='utf-8') as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)
        print(f"Output: {args.output}")
    else:
        with open(args.input, 'w', encoding='utf-8') as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)
        print(f"Output: {args.input}")


if __name__ == '__main__':
    main()