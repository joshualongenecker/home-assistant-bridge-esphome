#!/usr/bin/env python3
"""Auto-detect state_class from field names.

state_class is only meaningful for sensor domain entities.
- 'measurement': instantaneous values (temperature, voltage, current, etc.)
- 'total': cumulative counters that can go up and down (energy, water, gas)
- 'total_increasing': counters that only increase (cycle counts, runtime)

This script assigns state_class for fields with a device_class (using
keyword and device_class-based inference) and also for fields without
a device_class when the field name suggests a measurable quantity
(counter, time, level, position).
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pipeline_utils import SCRIPT_DIR, REPO_ROOT, load_json
from ha_constants import SENSOR_DEVICE_CLASS_STATE_CLASSES

def _word_bound(name_lower, kw):
    """Check if kw appears as a whole word (or at word boundary) in name_lower.

    Treats underscores as word separators so that 'my_cycle' matches 'cycle'.
    """
    normalized = name_lower.replace('_', ' ')
    return bool(re.search(r'\b' + re.escape(kw) + r'\b', normalized))

def infer_state_class(entry):
    """Infer state_class from field name, device_class, and ha_domain.

    state_class is ONLY valid for sensor domain entities.
    Returns (state_class, confidence) or (None, None).
    """
    field_name = entry.get('field_name', '')
    device_class = entry.get('review', {}).get('device_class')
    ha_domain = entry.get('review', {}).get('ha_domain')
    name_lower = field_name.lower()
    desc_lower = (entry.get('erd_description') or '').lower()
    combined = name_lower + ' ' + desc_lower

    if ha_domain != 'sensor':
        return None, None

    NON_NUMERIC_DEVICE_CLASSES = {'enum', 'timestamp', 'date', 'uptime'}
    if device_class in NON_NUMERIC_DEVICE_CLASSES:
        return None, None

    # --- Fields WITH device_class ---
    if device_class is not None:
        total_keywords = [
            'cumulative', 'total', 'consumption', 'usage', 'since clear',
        ]
        if any(kw in name_lower for kw in total_keywords):
            if 'average' in name_lower:
                return None, None
            if device_class in ('energy', 'gas', 'water', 'volume'):
                return 'total', 0.9

        total_increasing_keywords = [
            'count', 'number of', 'runtime', 'uptime',
        ]
        if any(kw in name_lower for kw in total_increasing_keywords):
            return 'total_increasing', 0.85

        if _word_bound(name_lower, 'cycle'):
            if device_class not in ('temperature',):
                return 'total_increasing', 0.8

        valid_states = SENSOR_DEVICE_CLASS_STATE_CLASSES.get(device_class, set())
        if 'measurement' in valid_states:
            if not any(kw in name_lower for kw in total_keywords):
                return 'measurement', 0.9

        return None, None

    counter_keywords = [
        'counter', 'count', 'total', 'cycle', 'reset', 'dispense',
        'door', 'error', 'fault', 'number of',
    ]
    time_keywords = [
        'time', 'duration', 'period', 'minute', 'second', 'hour', 'days',
    ]
    level_keywords = [
        'level', 'position', 'angle',
    ]
    if any(kw in combined for kw in counter_keywords + time_keywords + level_keywords):
        return 'measurement', 0.7

    return None, None

def apply_detection(entries):
    """Walk all entries, detect state_class, and overwrite review field.

    Only overwrites state_class when a new value is detected, preserving
    manually assigned state_class not inferred by the detector.
    """
    total_checked = 0
    total_matched = 0
    total_applied = 0

    for entry in entries:
        ha_domain = entry.get('review', {}).get('ha_domain')
        if ha_domain != 'sensor':
            continue

        total_checked += 1
        review = entry.setdefault('review', {})

        sc, confidence = infer_state_class(entry)
        if sc is None:
            continue

        if review.get('state_class'):
            continue

        total_matched += 1
        review['state_class'] = sc
        review['_sc_confidence'] = confidence
        total_applied += 1

    return total_checked, total_matched, total_applied

def main():
    parser = argparse.ArgumentParser(
        description='Auto-detect state_class from field names and device_class.'
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
    checked, matched, applied = apply_detection(entries)

    print(f"Checked {checked} fields with device_class")
    print(f"Matched {matched} fields with detectable state_class")
    print(f"Applied {applied} fields")

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