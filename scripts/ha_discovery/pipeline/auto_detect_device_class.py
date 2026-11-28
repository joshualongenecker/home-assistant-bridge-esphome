#!/usr/bin/env python3
"""Auto-detect device_class from field names and units.

Uses keyword matching from ha_constants.py, unit-based inference,
and exclusion rules to assign device_class. Fields that can't be
confidently classified are left as None for AI review.

Stale values from previous runs are cleared when the detector no longer
finds a match (e.g. keyword logic changed), ensuring the processed
JSON stays in sync with the current detection rules.

Architecture: unit-based detection runs first (highest confidence),
then keyword-based detection as fallback. This prevents keyword
false positives from overriding strong unit signals.
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pipeline_utils import SCRIPT_DIR, REPO_ROOT, load_json
from ha_constants import (
    DEVICE_CLASS_KEYWORDS,
    DEVICE_CLASS_EXCLUSIONS,
    UNIT_KEYWORD_MAP,
    VALID_DEVICE_CLASSES,
    SENSOR_NON_NUMERIC_DEVICE_CLASSES,
)
from auto_detect_ha_domain import PROTOCOL_MARKERS, BINARY_LABELS

def _word_bound(name_lower, kw):
    """Check if kw appears as a whole word (or at word boundary) in name_lower.

    Treats underscores as word separators (replaces them with spaces) so that
    'current_mA' matches 'current' and 'mA'.
    """
    normalized = name_lower.replace('_', ' ')
    return bool(re.search(r'\b' + re.escape(kw) + r'\b', normalized))

def _has_any(name_lower, kws):
    """Check if any keyword appears as substring in name_lower."""
    return any(kw in name_lower for kw in kws)

def _check_exclusions(name_lower, dc):
    """Check if name should be excluded for this device class.

    Returns True if the field should be excluded (not matched).
    Uses both standard exclusions from ha_constants and extra exclusions.
    All exclusions use word-boundary matching where possible.
    """
    standard = DEVICE_CLASS_EXCLUSIONS.get(dc, [])
    for exc in standard:
        if exc in name_lower:
            return True

    extra = {
        'power': [
            'power outage', 'powered', 'power/care', 'power consumption',
            'power saving', 'power mode', 'power button',
            'power-on', 'power diverter', 'power too high', 'power too low',
            'run time', 'cumulative', 'output power',
        ],
        'energy': [
            'save energy', 'energy usage update', 'energy usage minimum',
            'energy mode', 'energy saving',
        ],
        'temperature': [
            'version', 'revision', 'update period', 'step size',
            'color temperature', 'two-temp cook time',
        ],
        'frequency': [
            'feeding frequency', 'update frequency', 'polling frequency',
        ],
        'duration': [
            'command count', 'command counts', 'count', 'number of',
            'inter-byte', 'timeouts',
        ],
        'humidity': [
            'update period',
        ],
        'speed': [
            'run time',
        ],
        'volume': [
            'audio volume', 'flow rate',
        ],
        'gas': [
            'gas usage update', 'gas usage minimum',
        ],
        'water': [
            'water usage update', 'water usage minimum',
        ],
        'pm25': [
            'pm 1', 'pm 4', 'pm 10',
        ],
        'pressure': [
            'water level',
        ],
    }

    for exc in extra.get(dc, []):
        if exc in name_lower:
            return True

    return False

def infer_device_class(entry):
    """Infer device_class from field name, type, and unit.

    Returns (device_class, confidence) or (None, None).

    Order: unit-based first (highest confidence), then keyword-based.
    This prevents keyword false positives from overriding strong unit signals.
    """
    field_name = entry.get('field_name', '')
    field_type = entry.get('field_type', '')
    unit = entry.get('review', {}).get('unit_of_measurement')
    field_bits = entry.get('field_bits')
    ha_domain = entry.get('review', {}).get('ha_domain')
    name_lower = field_name.lower()
    erd_name = entry.get('erd_name', '')
    erd_description = entry.get('erd_description', '')
    combined = name_lower + ' ' + erd_name.lower() + ' ' + erd_description.lower()

    if ha_domain == 'binary_sensor' and not entry.get('review', {}).get('pair_role'):
        if 'occupied' in name_lower:
            return 'occupancy', 0.8
        if any(_word_bound(combined, kw) for kw in ['fault', 'issue', 'error', 'alarm', 'limited']):
            return 'problem', 0.8

    if field_bits is not None:
        return None, None

    if entry.get('field_values') is not None:
        values = entry.get('field_values', {})
        functional = {v for v in values.values() if v not in PROTOCOL_MARKERS}
        if len(functional) >= 3:
            return 'enum', 0.9
        if len(functional) == 2:
            labels = {v.lower() for v in functional}
            if not labels.issubset(BINARY_LABELS):
                return 'enum', 0.85

    if field_type not in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32'):
        return None, None

    # --- Unit-based detection (highest confidence, runs first) ---
    if unit:
        unit_to_dc = {
            '°F': 'temperature',
            '°C': 'temperature',
            'K': 'temperature',
            'deg F': 'temperature',
            'deg C': 'temperature',
            'V': 'voltage',
            'A': 'current',
            'mA': 'current',
            'W': 'power',
            'kWh': 'energy',
            'Wh': 'energy',
            'lb': 'weight',
            'g': 'weight',
            'Hz': 'frequency',
            'Pa': 'pressure',
            'inH₂O': 'pressure',
            'ft³': 'gas',
        }
        dc = unit_to_dc.get(unit)
        if dc:
            if _check_exclusions(name_lower, dc):
                return None, None
            return dc, 0.95

    # --- Keyword-based detection ---
    for dc, keywords in DEVICE_CLASS_KEYWORDS.items():
        if dc == 'duration':
            continue
        for kw in keywords:
            if len(kw) <= 2:
                continue

            if len(kw) <= 7:
                if not _word_bound(name_lower, kw):
                    continue
            else:
                if kw not in name_lower:
                    continue

            if _check_exclusions(name_lower, dc):
                continue

            return dc, 0.8
    # --- Extra keyword categories not in ha_constants ---
    extra_keywords = {
        'gas': ['gas'],
        'water': ['water usage'],
    }
    for dc, keywords in extra_keywords.items():
        if dc == 'duration':
            continue
        for kw in keywords:
            if len(kw) <= 2:
                continue
            if len(kw) <= 7:
                if not _word_bound(name_lower, kw):
                    continue
            else:
                if kw not in name_lower:
                    continue
            if _check_exclusions(name_lower, dc):
                continue
            return dc, 0.8

    return None, None

def apply_detection(entries):
    """Walk all entries, detect device_class, and overwrite review field.

    Only overwrites device_class when a new value is detected, preserving
    manually assigned device_class values not inferred by the detector.
    """
    total_checked = 0
    total_matched = 0
    total_applied = 0

    for entry in entries:
        field_type = entry.get('field_type', '')
        if field_type not in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32', 'enum', 'bool'):
            continue
        total_checked += 1
        review = entry.setdefault('review', {})

        dc, confidence = infer_device_class(entry)
        if dc is None:
            if review.get('device_class') is not None:
                review['device_class'] = None
            continue

        review['device_class'] = dc
        review['_dc_confidence'] = confidence
        total_matched += 1
        total_applied += 1

    return total_checked, total_matched, total_applied

def main():
    parser = argparse.ArgumentParser(
        description='Auto-detect device_class from field names and units.'
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

    print(f"Checked {checked} numeric fields")
    print(f"Matched {matched} fields with detectable device_class")
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