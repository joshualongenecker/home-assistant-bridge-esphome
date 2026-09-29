#!/usr/bin/env python3
"""Auto-detect ha_domain from field type, device_class, and operations.

ha_domain and filtered are independent. This script assigns ha_domain to
ALL fields regardless of whether they should be filtered. Filtering is
a separate review step.

AGENTS.md rules:
- bool + writable -> switch; bool + read-only -> binary_sensor; bool + one-shot -> button
- enum 2-value on/off keyed 0/1 + writable -> switch; read-only -> binary_sensor
- enum 2-value descriptive, or on/off keyed other than 0/1 (e.g. 1/2)
  + writable -> select; read-only -> sensor
- enum >2-value + writable -> select; read-only -> sensor
- numeric + read-only -> sensor; writable -> number
- string -> sensor
- bit-field 1-bit -> binary_sensor (or switch if writable)
- bit-field multi-bit -> sensor (or number if writable)
- raw -> None (left for AI review)
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pipeline_utils import SCRIPT_DIR, REPO_ROOT, load_json
from ha_constants import (
    DEVICE_CLASS_KEYWORDS,
    DEVICE_CLASS_EXCLUSIONS,
    VALID_DEVICE_CLASSES,
    SENSOR_NON_NUMERIC_DEVICE_CLASSES,
    BINARY_SENSOR_DEVICE_CLASSES,
    PROTOCOL_MARKERS,
)


def _is_writable(entry):
    """Check if field is writable based on ERD operations."""
    operations = entry.get('erd_operations', [])
    return 'write' in operations


def _is_enum(entry):
    """Check if field has enum values."""
    return entry.get('field_values') is not None

def _enum_value_count(entry):
    """Count enum values, excluding protocol markers."""
    values = entry.get('field_values', {})
    if not values:
        return 0
    count = 0
    for v, label in values.items():
        if label in PROTOCOL_MARKERS:
            continue
        count += 1
    return count


def _functional_labels(entry):
    """Get enum labels excluding protocol markers, lowercased."""
    values = entry.get('field_values', {})
    if not values:
        return set()
    return {v.lower() for v in values.values() if v not in PROTOCOL_MARKERS}


# Comprehensive set of binary-like labels.
BINARY_LABELS = {
    'on', 'off',
    'true', 'false',
    'enabled', 'disabled',
    'enable', 'disable',
    'locked', 'not locked', 'unlocked',
    'active', 'inactive',
    'start', 'stop',
    'open', 'closed',
    'running', 'stopped',
    'ai disabled', 'ai enabled',
    'sleep mode off', 'sleep mode on',
    'eco mode off', 'eco mode on',
    'delaystart disabled', 'delaystart enabled',
    'clean filter light off', 'clean filter light on',
    'delay start disabled', 'delay start enabled',
    'present', 'not present',
    'supported', 'not supported',
    'available', 'not available',
    'clear', 'set',
    'empty', 'full',
    'completed', 'not completed',
    'lid lock', 'lid unlock',
    'go to agitate', 'go to spin',
    'close', 'open',
    'ui locked', 'ui not locked',
    'start command', 'normal state',
    'do nothing', 'request gas valve to be locked',
    'write to return to normal operation', 'write to cycle to full cold',
    'add 30 seconds', 'no action',
}

# Labels that indicate a descriptive command, NOT a binary toggle
DESCRIPTIVE_COMMANDS = {
    'no action required', 'reset filter',
    'go to agitate', 'go to spin',
}


def _is_binary_enum(entry):
    """Check if enum values form a binary pair (on/off-like semantics).

    After excluding protocol markers, if exactly 2 labels remain and
    both are in the binary set, it's a binary enum.
    """
    labels = _functional_labels(entry)
    if len(labels) != 2:
        return False

    # If both labels are in the binary set, it's binary
    if labels.issubset(BINARY_LABELS):
        # But if both are in descriptive commands, it's NOT binary
        if labels.issubset(DESCRIPTIVE_COMMANDS):
            return False
        return True

    return False


def _binary_enum_keyed_zero_one(entry):
    """Check that a binary enum's functional values are exactly 0 and 1.

    switch and binary_sensor entities encode state as '00'/'01'. Binary enums
    keyed on other values (e.g. 1=Open, 2=Closed) cannot round-trip through
    those payloads and must be exposed as select/sensor with their labels.
    """
    values = entry.get('field_values', {}) or {}
    keys = set()
    for k, label in values.items():
        if label in PROTOCOL_MARKERS:
            continue
        try:
            keys.add(int(k))
        except (TypeError, ValueError):
            return False
    return keys == {0, 1}


def _is_bit_field(entry):
    """Check if field is a bit-field (field_bits is a dict)."""
    bits = entry.get('field_bits')
    return isinstance(bits, dict)


def _bit_field_size(entry):
    """Get bit-field size in bits."""
    bits = entry.get('field_bits', {})
    return bits.get('size', 0)


def infer_ha_domain(entry):
    """Infer ha_domain from field type, device_class, and operations.

    Returns (ha_domain, confidence) or (None, None).
    """
    field_type = entry.get('field_type', '')
    device_class = entry.get('review', {}).get('device_class')
    writable = _is_writable(entry)
    field_name = entry.get('field_name', '')
    name_lower = field_name.lower()

    # --- Enum type (check before bit-field for descriptive enums) ---
    if _is_enum(entry):
        count = _enum_value_count(entry)
        if count == 0:
            return None, None

        if count == 2:
            if _is_binary_enum(entry) and _binary_enum_keyed_zero_one(entry):
                # Binary enum: on/off-like semantics
                if writable:
                    return 'switch', 0.9
                return 'binary_sensor', 0.9
            else:
                # Descriptive 2-value enum
                if writable:
                    return 'select', 0.85
                return 'sensor', 0.85
        else:
            # Multi-value enum (3+)
            if writable:
                return 'select', 0.9
            return 'sensor', 0.9

    # --- Raw type ---
    if field_type == 'raw':
        return None, None

    # --- Bool type (before bit-field, as bool takes precedence) ---
    if field_type == 'bool':
        if writable:
            if any(kw in name_lower for kw in ['restart', 'factory reset', 'clear', 'reset', 'cancel']):
                return 'button', 0.9
            return 'switch', 0.9
        return 'binary_sensor', 0.9

    # --- Bit-fields (only if not bool or enum) ---
    if _is_bit_field(entry):
        if _bit_field_size(entry) == 1:
            if writable:
                return 'switch', 0.9
            return 'binary_sensor', 0.9
        # Multi-bit fields: writable -> number, read-only -> sensor
        if writable:
            return 'number', 0.85
        return 'sensor', 0.85


    # --- String type ---
    if field_type == 'string':
        return 'sensor', 0.9

    # --- Numeric types ---
    if field_type in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32'):
        if writable:
            return 'number', 0.9
        return 'sensor', 0.9

    return None, None


def apply_detection(entries):
    """Walk all entries, detect ha_domain, and overwrite review field.

    Only overwrites ha_domain when a new value is detected, preserving
    manually assigned domains not inferred by the detector.
    """
    total_checked = 0
    total_matched = 0
    total_applied = 0

    for entry in entries:
        total_checked += 1
        review = entry.setdefault('review', {})

        domain, confidence = infer_ha_domain(entry)
        if domain is None:
            continue

        # Only write when not already set, preserving manual overrides.
        if review.get('ha_domain'):
            continue

        total_matched += 1
        review['ha_domain'] = domain
        review['_domain_confidence'] = confidence
        total_applied += 1

    return total_checked, total_matched, total_applied




def main():
    parser = argparse.ArgumentParser(
        description='Auto-detect ha_domain from field type, device_class, and operations.'
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

    print(f"Checked {checked} fields")
    print(f"Matched {matched} fields with detectable ha_domain")
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