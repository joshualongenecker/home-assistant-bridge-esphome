#!/usr/bin/env python3
"""
Script to generate Home Assistant MQTT Discovery JSONL files from the
public-appliance-api-documentation ERD definitions.

Reads appliance_api_erd_definitions.json and produces category-specific
JSONL files in ha_discovery/, each line being a compact JSON object
defining one HA entity.

Uses ERD data type information to generate proper value/command templates:
  - Signed integer types (i8, i16, i32) get two's-complement conversion
  - Scaling factors are applied with proper decimal places
  - Enum types get proper hex-to-label mapping
  - Multi-field ERDs are classified (single/byte_offset/bitfield/mixed)
  - Bit-field sub-values are extracted with proper masking

Each JSONL line has these keys:
  i  - ERD ID (lowercase hex, zero-padded to 4 chars)
  n  - Entity name (human-readable)
  d  - HA domain: sensor, binary_sensor, switch, select, number, button
  ds - ERD data size in bytes (total ERD payload size)
  vt - Jinja2 value_template for decoding the hex payload
  ct - Jinja2 command_template for encoding commands (writable ERDs)
  u  - unit_of_measurement
  dc - device_class
  sc - state_class (e.g. "total", "measurement")
  fi - Field ID for sub-fields within a multi-byte ERD
  p  - Paired ERD ID (hex string)
  r  - Role: "request" or "status"
  o  - JSON array of options (for select domain)
  dt - Data type for number domain
  sf - Scale factor for number domain
"""

import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).parent.parent))
from pipeline.ha_constants import VALID_DEVICE_CLASSES, CATEGORIES


def _is_valid_device_class(domain: str, device_class: str) -> bool:
    """Check if device_class is valid for the given HA domain."""
    if not device_class:
        return True
    valid = VALID_DEVICE_CLASSES.get(domain)
    if valid is None:
        return True  # no restrictions for this domain
    return device_class in valid


# ---------------------------------------------------------------------------
# Basic helpers
# ---------------------------------------------------------------------------

def parse_erd_id(erd_id_str: str) -> int:
    """Convert ERD ID string (e.g., '0x0001') to integer."""
    return int(erd_id_str, 16)


def erd_id_to_hex(erd_id_str: str) -> str:
    """Convert ERD ID string to lowercase hex, zero-padded to 4 chars."""
    return format(parse_erd_id(erd_id_str), '04x')


def get_category(erd_id: int) -> Optional[str]:
    """Return the category name for an ERD ID, or None if unclassified."""
    for name, (lo, hi) in CATEGORIES.items():
        if lo <= erd_id <= hi:
            return name
    return None


def get_erd_byte_size(erd_data: List[Dict]) -> int:
    """Compute the actual byte size of an ERD from its data field definitions.

    Each data field has an 'offset' (byte offset) and 'size' (byte count).
    Fields may overlap (bit-fields share the same bytes), so the true ERD byte
    size is the highest (offset + size) value across all fields.
    """
    if not erd_data:
        return 0
    return max((d.get('offset', 0) + d.get('size', 0)) for d in erd_data)


def get_first_enum_values(erd_data: List[Dict]) -> Dict[str, str]:
    """Return the values dict from the first enum-typed data field, or {}."""
    for d in erd_data:
        if d.get('type') == 'enum':
            return d.get('values', {})
    return {}


def _get_first_enum_field_info(erd_data: List[Dict]) -> Tuple[Dict[str, str], int]:
    """Return (values_dict, field_size) for the first enum-typed data field.

    field_size is the byte width of the enum field (used to build the hex
    extraction slice in value templates).  Falls back to ({}, 1) when no
    enum field is present.
    """
    for d in erd_data:
        if d.get('type') == 'enum':
            return d.get('values', {}), max(1, d.get('size', 1))
    return {}, 1


# ---------------------------------------------------------------------------
# Multi-field ERD helpers
# ---------------------------------------------------------------------------

def _is_signed_type(type_str: str) -> bool:
    """Return True if the type string represents a signed integer (e.g. 'i8', 'i16', 'i32')."""
    return bool(re.match(r'^i\d+$', type_str))


def _is_float32_type(type_str: str) -> bool:
    """Return True for a four-byte IEEE-754 single-precision value."""
    return type_str == 'float32'

def _compute_number_range(data_type: str, scaling_factor: int) -> Tuple[float, float, float]:
    """Return (min, max, step) for a number entity based on data type and scale factor.

    Returns user-facing values (after scaling), not raw byte values.
    E.g. u16 with sf=10 -> (0, 6553.5, 0.1)
    """
    bounds = {
        'u8': (0, 255),
        'i8': (-128, 127),
        'u16': (0, 65535),
        'i16': (-32768, 32767),
        'u32': (0, 4294967295),
        'i32': (-2147483648, 2147483647),
    }
    # Defensive: scaling_factor=0 would cause ZeroDivisionError.
    # post_process.py Rule 4 should catch this, but guard here too.
    if scaling_factor <= 0:
        scaling_factor = 1
    raw_min, raw_max = bounds.get(data_type, (0, 255))
    step = 1.0 / scaling_factor
    return (raw_min / scaling_factor, raw_max / scaling_factor, step)


def _get_primary_data_type(erd_data: List[Dict]) -> str:
    """Return the type of the primary (first non-reserved, non-bitfield) data field."""
    for d in erd_data:
        if not _is_reserved_field(d.get('name', '')) and not _has_bits(d):
            return d.get('type', 'u8')
    return 'u8'

def _jinja2_escape(s: str) -> str:
    """Escape a string for safe use inside a single-quoted Jinja2 dict literal.

    Replaces single quotes with backslash-escaped single quotes so that
    values like "Don't Care" don't break the dict literal syntax.
    """
    return s.replace("'", "\\'")

def _is_reserved_field(name: str) -> bool:
    """Return True if a field name indicates it is a reserved/padding field."""
    n = name.lower()
    return 'reserved' in n or 'padding' in n


def _leaf_field_name(name: str) -> str:
    """Return the leaf portion of a potentially dot-qualified field name.

    E.g. "Allowed Selections.Cyclic Supported" -> "Cyclic Supported".
    Handles edge cases like "Air Purifier.PM2.5" where the dot is part of
    the value (decimal/unicode) rather than a namespace separator.
    """
    parts = name.split('.')
    if len(parts) == 1:
        return name.strip()
    leaf = parts[-1].strip()
    # If the last part is purely numeric and the second-to-last part ends
    # with an alphanumeric char (letter or digit), the dot is likely part
    # of a decimal or version number (e.g., "PM2.5" -> "PM2" + ".5").
    if len(parts) >= 2 and leaf.isdigit() and parts[-2][-1:].isalnum():
        return (parts[-2] + '.' + leaf).strip()
    return leaf


def _clean_field_name(name: str) -> str:
    """Strip parenthetical suffixes (scaling/unit info) from a field name.

    E.g. 'Hours (hours)' -> 'Hours',
         'Line Input Voltage Volts x 100 (volts)' -> 'Line Input Voltage Volts x 100',
         'Option n Drying Temperature (Fahrenheit x 10)[0]' -> 'Option n Drying Temperature[0]'.
    """
    # Keep trailing array index like [0], [1], etc. for uniqueness
    # (removed: name = re.sub(r'\s*\[\d+\]\s*$', '', name))
    # Remove parenthetical groups: either trailing, or before an array index
    result = re.sub(r'\s*\([^)]*\)\s*(?=\[\d+\])', '', name)
    result = re.sub(r'\s*\([^)]*\)\s*$', '', result)
    return result.strip()


def _field_slug(name: str) -> str:
    """Convert a field name to a compact ASCII slug suitable for unique_ids.

    Examples:
        "Critical Major"          -> "critical_major"
        "GH (Fan Hi)"             -> "gh_fan_hi"
        "Cyclic Supported"        -> "cyclic_supported"
    """
    slug = re.sub(r'[^a-z0-9]+', '_', name.lower())
    slug = slug.strip('_')
    # Cap at 64 chars to keep MQTT topic segments reasonable without losing
    # enough uniqueness to cause collisions within a single ERD's sub-fields.
    return slug[:64]


def _has_bits(field: Dict) -> bool:
    """Return True if a field carries a 'bits' sub-object (i.e. it is a bit-field)."""
    return 'bits' in field and isinstance(field['bits'], dict)


def _get_non_reserved_fields(erd_data: List[Dict]) -> List[Dict]:
    """Return data fields whose names do not indicate reserved/padding content."""
    return [d for d in erd_data if not _is_reserved_field(d.get('name', ''))]


def _is_version_field(name: str) -> bool:
    """Return True if a field name matches a version component pattern.

    Matches: Critical Major, Critical Minor, Non-Critical Major, Non-Critical Minor
    (with optional prefix like "UI ", "MC ", "Inverter ", etc. and optional " Version" suffix).
    """
    n = name.lower()
    for pattern in ('critical major', 'critical minor', 'non-critical major', 'non-critical minor'):
        if pattern in n:
            return True
    return False


def _version_field_role(name: str) -> Optional[str]:
    """Return the version role of a field, or None if not a version field.

    Returns one of: 'crit_major', 'crit_minor', 'noncrit_major', 'noncrit_minor'.
    """
    n = name.lower()
    # Check non-critical before critical to avoid substring match
    # ("critical major" is a substring of "non-critical major")
    if 'non-critical major' in n:
        return 'noncrit_major'
    if 'non-critical minor' in n:
        return 'noncrit_minor'
    if 'critical major' in n:
        return 'crit_major'
    if 'critical minor' in n:
        return 'crit_minor'
    return None


def _is_parametric_field(name: str) -> bool:
    """Return True if a field name is a parametric version component."""
    n = name.lower()
    return ('parametric major' in n) or ('parametric minor' in n)


def _extract_board_prefix(name: str) -> str:
    """Extract the board prefix from a field name like 'UI Critical Major Version'.

    Returns the prefix before the version role keyword, stripped.
    E.g. 'UI Critical Major Version' -> 'UI', 'Critical Major' -> ''.
    """
    n = name.strip()
    # Check longer keywords first to avoid substring matches
    for keyword in ('Non-Critical Major', 'Non-Critical Minor',
                    'Critical Major', 'Critical Minor',
                    'Parametric Major', 'Parametric Minor'):
        idx = n.find(keyword)
        if idx >= 0:
            return n[:idx].rstrip()
    return n


def _is_simple_version_erd(erd_data: List[Dict]) -> bool:
    """Return True if the ERD is a simple 4-byte version ERD.

    Must have exactly 4 non-reserved u8 fields at offsets 0-3 with the pattern:
    Critical Major, Critical Minor, Non-Critical Major, Non-Critical Minor.
    """
    nr = _get_non_reserved_fields(erd_data)
    if len(nr) != 4:
        return False
    expected_roles = ['crit_major', 'crit_minor', 'noncrit_major', 'noncrit_minor']
    for i, field in enumerate(nr):
        if field.get('type') != 'u8':
            return False
        if field.get('offset', 0) != i:
            return False
        role = _version_field_role(field.get('name', ''))
        if role != expected_roles[i]:
            return False
    return True


def _group_multi_board_version_fields(erd_data: List[Dict]) -> List[Dict]:
    """Group fields of a multi-board version ERD by board prefix.

    Returns a list of groups, each with:
      - 'prefix': board name (e.g. 'UI', 'MC')
      - 'version_fields': list of 4 version fields in order (crit_major, crit_minor, noncrit_major, noncrit_minor)
      - 'parametric_fields': list of 2 parametric fields (major, minor) or empty
    """
    nr = _get_non_reserved_fields(erd_data)
    if not nr:
        return []

    # Group fields by board prefix
    boards: Dict[str, Dict[str, Dict]] = {}
    for field in nr:
        name = field.get('name', '')
        prefix = _extract_board_prefix(name)
        if prefix not in boards:
            boards[prefix] = {}
        role = _version_field_role(name)
        if role:
            boards[prefix][role] = field
        elif _is_parametric_field(name):
            n = name.lower()
            p_role = 'parametric_major' if 'parametric major' in n else 'parametric_minor'
            boards[prefix][p_role] = field

    # Filter to boards that have all 4 version components
    required = {'crit_major', 'crit_minor', 'noncrit_major', 'noncrit_minor'}
    groups = []
    for prefix, roles in boards.items():
        if required.issubset(roles.keys()):
            groups.append({
                'prefix': prefix,
                'version_fields': [
                    roles['crit_major'],
                    roles['crit_minor'],
                    roles['noncrit_major'],
                    roles['noncrit_minor'],
                ],
                'parametric_fields': [
                    roles.get('parametric_major'),
                    roles.get('parametric_minor'),
                ],
            })
    return groups


def _classify_erd_data(erd_data: List[Dict]) -> str:
    """Classify how sub-fields of an ERD should be expanded for HA discovery.

    Returns one of:
        'single'              - one entity covers the whole ERD value
        'byte_offset'         - multiple fields at distinct byte positions (no bits)
        'bitfield'            - all non-reserved fields are bit-flags at same byte range
        'mixed'               - one primary byte-offset field + additional bit-flag fields
        'version'             - 4-part version ERD (crit.major.noncrit.major.noncrit.minor)
        'multi_board_version' - multi-board version ERD (e.g. dishwasher system software)
    """
    nr = _get_non_reserved_fields(erd_data)
    if len(nr) <= 1:
        return 'single'

    # Check for simple 4-byte version ERD before other classifications
    if _is_simple_version_erd(erd_data):
        return 'version'

    # Check for multi-board version ERD
    groups = _group_multi_board_version_fields(erd_data)
    if groups:
        # Only classify as multi-board if ALL non-reserved fields are accounted for
        total_accounted = 0
        for g in groups:
            total_accounted += len(g['version_fields'])
            total_accounted += len([f for f in g['parametric_fields'] if f is not None])
        if total_accounted == len(nr):
            return 'multi_board_version'

    with_bits = [d for d in nr if _has_bits(d)]
    no_bits = [d for d in nr if not _has_bits(d)]

    if not with_bits:
        # All byte-offset: split only if they occupy different (offset, size) ranges
        offsets = {(d.get('offset', 0), d.get('size', 1)) for d in no_bits}
        return 'byte_offset' if len(offsets) > 1 else 'single'
    elif not no_bits:
        # Pure bit-field ERD
        return 'bitfield'
    else:
        # Mixed: primary value field + bit-flag fields
        return 'mixed'


# ---------------------------------------------------------------------------
# Value template generators
# ---------------------------------------------------------------------------

def _byte_subfield_value_template(field: Dict, erd_scaling: int) -> str:
    """Generate a Jinja2 value_template that extracts one byte-offset sub-field.

    The template slices the right hex-char range from the full ERD hex payload,
    then converts to a number (applying scaling if needed) or an enum label.
    """
    offset = field.get('offset', 0)
    size = field.get('size', 1)
    hex_start = offset * 2
    hex_end = (offset + size) * 2
    field_type = field.get('type', 'u8')

    if field_type == 'enum':
        enum_values = field.get('values', {})
        valid_pairs = sorted(
            [(int(k), v) for k, v in enum_values.items() if v != 'Request Consumed'],
            key=lambda x: x[0]
        )
        if not valid_pairs:
            return f"{{{{ value[{hex_start}:{hex_end}] }}}}"
        hex_chars = size * 2
        mapping = ', '.join(f"'{k:0{hex_chars}x}': '{_jinja2_escape(v)}'" for k, v in valid_pairs)
        return f"{{{{ {{{mapping}}}.get(value[{hex_start}:{hex_end}], 'Unknown') }}}}"
    elif field_type == 'string':
        return _string_subfield_value_template(field)
    elif field_type == 'bool':
        return f"{{{{ '01' if value[{hex_start}:{hex_end}] != '00' else '00' }}}}"
    elif _is_float32_type(field_type):
        # ERD MQTT state payloads are hexadecimal bytes. Home Assistant's
        # from_hex/unpack filters decode the GEA big-endian IEEE-754 payload.
        return f"{{{{ value[{hex_start}:{hex_end}] | from_hex | unpack('>f') | round(3) }}}}"
    else:
        # Numeric types: u8, u16, u32, i8, i16, i32, etc.
        if _is_signed_type(field_type):
            max_val = 2 ** (size * 8)
            half_val = max_val // 2
            if erd_scaling and erd_scaling > 1:
                dp = {10: 1, 100: 2}.get(erd_scaling, 3)
                return (f"{{{{ ((value[{hex_start}:{hex_end}] | int(base=16)) - {max_val}"
                        f" if (value[{hex_start}:{hex_end}] | int(base=16)) >= {half_val}"
                        f" else (value[{hex_start}:{hex_end}] | int(base=16)))"
                        f" / {erd_scaling} | round({dp}) }}}}")
            else:
                return (f"{{{{ (value[{hex_start}:{hex_end}] | int(base=16)) - {max_val}"
                        f" if (value[{hex_start}:{hex_end}] | int(base=16)) >= {half_val}"
                        f" else (value[{hex_start}:{hex_end}] | int(base=16)) }}}}")
        elif erd_scaling and erd_scaling > 1:
            dp = {10: 1, 100: 2}.get(erd_scaling, 3)
            return (f"{{{{ (value[{hex_start}:{hex_end}] | int(base=16))"
                    f" / {erd_scaling} | round({dp}) }}}}")
        else:
            return f"{{{{ value[{hex_start}:{hex_end}] | int(base=16) }}}}"
def _string_subfield_value_template(field: Dict) -> str:
    """Generate a Jinja2 value_template that decodes a string sub-field from hex.

    Converts each hex byte pair in the sliced range to ASCII, skipping
    null bytes and stripping trailing '_' padding.
    """
    offset = field.get('offset', 0)
    size = field.get('size', 1)
    hex_start = offset * 2
    hex_end = (offset + size) * 2

    chars = ''.join(chr(i) for i in range(0x20, 0x7F))
    chars_escaped = chars.replace("'", "\\'")
    return (
        "{% set chars = '" + chars_escaped + "' %}"
        "{% set ns = namespace(value='') %}"
        "{% set slice = value[" + str(hex_start) + ":" + str(hex_end) + "] %}"
        "{% for i in range(0, slice | length, 2) %}"
        "{% set b = slice[i:i+2] | int(base=16) %}"
        "{% if b >= 0x20 and b <= 0x7E %}"
        "{% set ns.value = ns.value ~ chars[b - 0x20] %}"
        "{% endif %}"
        "{% endfor %}"
        "{{ ns.value.rstrip('_') }}"
    )


def _bitfield_sub_value_template(field: Dict) -> str:
    """Generate a Jinja2 value_template that extracts one bit-field sub-field.

    For 1-bit fields the template outputs '01' (on) or '00' (off) so that it
    works with the binary_sensor payload_on/payload_off defaults already
    hardcoded in publish_next_ha_discovery_entity_().
    For multi-bit fields the template outputs the extracted integer.
    """
    byte_offset = field.get('offset', 0)
    byte_size = field.get('size', 1)
    bits = field.get('bits', {})
    bit_offset = bits.get('offset', 0)
    bit_size = bits.get('size', 1)
    hex_start = byte_offset * 2
    hex_end = (byte_offset + byte_size) * 2

    if bit_size == 1:
        divisor = 2 ** bit_offset
        return (f"{{{{ '01' if ((value[{hex_start}:{hex_end}] | int(base=16))"
                f" // {divisor}) % 2 else '00' }}}}")
    else:
        divisor = 2 ** bit_offset
        modulus = 1 << bit_size
        return (f"{{{{ ((value[{hex_start}:{hex_end}] | int(base=16))"
                f" // {divisor}) % {modulus} }}}}")

def _version_value_template(fields: List[Dict]) -> str:
    """Generate a Jinja2 value_template for a 4-part version ERD.

    Produces a dotted decimal string like '1.0.2.3' from 4 u8 fields
    at consecutive byte offsets.
    """
    parts = []
    for field in fields:
        offset = field.get('offset', 0)
        hex_start = offset * 2
        hex_end = (offset + 1) * 2
        parts.append(f"(value[{hex_start}:{hex_end}] | int(base=16) | string)")
    return '{{ ' + ' + \".\" + '.join(parts) + ' }}'


def _parametric_version_value_template(fields: List[Dict]) -> str:
    """Generate a Jinja2 value_template for a 2-part parametric version.

    Produces a dotted decimal string like '1.2' from 2 u8 fields.
    """
    parts = []
    for field in fields:
        if field is None:
            return ''
        offset = field.get('offset', 0)
        hex_start = offset * 2
        hex_end = (offset + 1) * 2
        parts.append(f"(value[{hex_start}:{hex_end}] | int(base=16) | string)")
    if len(parts) != 2:
        return ''
    return '{{ ' + ' + \".\" + '.join(parts) + ' }}'


def _unit_to_ha(unit: str) -> str:
    """Convert an API unit string to the Home Assistant display unit."""
    return {'degF': '\u00b0F', 'degC': '\u00b0C'}.get(unit, unit)


def _compute_sensor_value_template(scaling_factor: int, data_size: int, signed: bool = False,
                                   float32: bool = False) -> str:
    """Return the Jinja2 value_template for a numeric sensor ERD.

    When ``signed`` is True the template applies two's-complement sign extension
    so that negative values (e.g. an int16 encoded as 0xFFFF) are reported as
    negative numbers rather than large positive values.
    """
    if float32:
        # GEA multi-byte ERDs are sent most-significant byte first. The
        # Home Assistant template functions have been available since 2023.4.
        return "{{ value | from_hex | unpack('>f') | round(3) }}"
    if signed:
        max_val = 2 ** (data_size * 8)
        half_val = max_val // 2
        if scaling_factor > 1:
            dp = {10: 1, 100: 2}.get(scaling_factor, 3)
            return (f'{{{{ ((value | int(base=16)) - {max_val}'
                    f' if (value | int(base=16)) >= {half_val}'
                    f' else (value | int(base=16))) / {scaling_factor} | round({dp}) }}}}')
        return (f'{{{{ (value | int(base=16)) - {max_val}'
                f' if (value | int(base=16)) >= {half_val}'
                f' else (value | int(base=16)) }}}}')
    if scaling_factor > 1:
        dp = {10: 1, 100: 2}.get(scaling_factor, 3)
        return f'{{{{ (value | int(base=16)) / {scaling_factor} | round({dp}) }}}}'
    return '{{ value | int(base=16) }}'


def _string_value_template(data_size: int) -> str:
    """Return a Jinja2 value_template for string-type ERDs.

    GE API model/serial are plain ASCII. The MQTT payload is hex,
    so the template converts each hex byte pair to ASCII, skipping
    null bytes and stripping trailing '_' padding.
    """
    # Build a lookup string for ASCII 0x20-0x7E (printable range).
    # Index 0 maps to 0x20 (' '), index 0x5E maps to 0x7E ('~').
    chars = ''.join(chr(i) for i in range(0x20, 0x7F))
    chars_escaped = chars.replace("'", "\\'")
    return (
        "{% set chars = '" + chars_escaped + "' %}"
        "{% set ns = namespace(value='') %}"
        "{% for i in range(0, value | length, 2) %}"
        "{% set b = value[i:i+2] | int(base=16) %}"
        "{% if b >= 0x20 and b <= 0x7E %}"
        "{% set ns.value = ns.value ~ chars[b - 0x20] %}"
        "{% endif %}"
        "{% endfor %}"
        "{{ ns.value.rstrip('_') }}"
    )


def _infer_unit_from_field_name(field_name: str, parent_unit: str) -> str:
    """Override the parent ERD unit when the field name explicitly names a unit.

    This handles ERDs like 0x7705 which has one field in degF and one in degC but
    the parent unit_of_measurement is degF.  If the field name contains the
    word 'Celsius' the unit is overridden to degC, and vice versa for 'Fahrenheit'.
    Otherwise the parent unit is returned unchanged.
    """
    name_lower = field_name.lower()
    if 'celsius' in name_lower:
        return '\u00b0C'
    if 'fahrenheit' in name_lower:
        return '\u00b0F'
    return parent_unit


def _compute_binary_sensor_value_template(data_size: int) -> str:
    """Return value_template for a binary_sensor ERD.

    The MQTT payload is raw hex (e.g. '00', '01'). HA's default payload_on/off
    are 'ON'/'OFF', so we always need a template to convert.
    For multi-byte ERDs we slice the first two hex chars to get the first byte.
    """
    hex_chars = data_size * 2
    tmpl = f"value[:{hex_chars}]" if hex_chars > 2 else "value"
    return f"{{{{ 'ON' if {tmpl} | int(base=16) != 0 else 'OFF' }}}}"
def _compute_switch_value_template(data_size: int) -> str:
    """Return empty value_template for switch.

    Switches use state_on/state_off and payload_on/payload_off instead of
    value_template, since HA ignores state_on/off when value_template is set.
    """
    return ''


def _paired_primary_field_template(erd_by_id: Dict[str, Dict], paired_erd_str: str,
                                    erd_scaling: int, for_switch: bool = False) -> Optional[str]:
    """Return a Jinja2 value_template that extracts the primary field from a
    paired ERD's full hex payload, or None if the paired ERD isn't found or
    has no non-bitfield primary field.

    When for_switch=True, returns a raw hex extraction template (e.g. value[0:2])
    so the output matches state_on/state_off. Otherwise uses the full field
    template (enum labels, scaling, etc).
    """
    pf = _get_primary_field(erd_by_id, paired_erd_str)
    if pf is None:
        return None
    if for_switch:
        # Extract just the hex bytes for the primary field.
        offset = pf.get('offset', 0)
        size = pf.get('size', 1)
        hex_start = offset * 2
        hex_end = (offset + size) * 2
        return f"{{{{ value[{hex_start}:{hex_end}] }}}}"
    return _byte_subfield_value_template(pf, erd_scaling)

def _select_options_and_templates(enum_values: Dict[str, str], data_size: int):
    """Build options_json, value_template and command_template for a select entity.

    'Request Consumed' (value 255) is excluded from selectable options because
    it is a write-only protocol marker (not a valid user-visible state).
    Returns (options_json_str, value_template_str, command_template_str).
    """
    # Filter out 'Request Consumed' (255) and sort by numeric key
    valid_pairs = sorted(
        [(int(k), v) for k, v in enum_values.items() if v != 'Request Consumed'],
        key=lambda x: x[0]
    )
    if not valid_pairs:
        return ('[]', '', '')

    hex_chars = data_size * 2

    # Build value_template: map hex string -> option name
    hex_to_name = ', '.join(
        f"'{k:0{hex_chars}x}': '{_jinja2_escape(v)}'" for k, v in valid_pairs
    )
    value_template = f"{{{{ {{{hex_to_name}}}.get(value[:{hex_chars}], 'Unknown') }}}}"

    # Build command_template: map option name -> hex string
    name_to_hex = ', '.join(
        f"'{_jinja2_escape(v)}': '{k:0{hex_chars}x}'" for k, v in valid_pairs
    )
    command_template = f"{{{{ {{{name_to_hex}}}[value] }}}}"

    # Build options JSON array
    option_names = [v for _, v in valid_pairs]
    options_json = '[' + ', '.join(f'"{name}"' for name in option_names) + ']'

    return (options_json, value_template, command_template)


def _enum_sensor_value_template(enum_values: Dict[str, str], field_size: int) -> str:
    """Build value_template for a read-only enum sensor.

    Maps hex byte values to their human-readable label.  Works the same as the
    select value_template but without options or command_template.
    'Request Consumed' (255) is excluded.
    Falls back to showing the raw first-byte hex string when no valid mappings
    exist.
    """
    valid_pairs = sorted(
        [(int(k), v) for k, v in enum_values.items() if v != 'Request Consumed'],
        key=lambda x: x[0]
    )
    if not valid_pairs:
        return '{{ value[:2] }}'

    hex_chars = field_size * 2
    hex_to_name = ', '.join(
        f"'{k:0{hex_chars}x}': '{_jinja2_escape(v)}'" for k, v in valid_pairs
    )
    return f"{{{{ {{{hex_to_name}}}.get(value[:{hex_chars}], 'Unknown') }}}}"


def _strip_pair_role_word(name: str) -> str:
    """Remove trailing or standalone 'Status'/'Request' words from a paired-ERD name.

    Examples:
        'Fan Configuration in Cooling Status'  -> 'Fan Configuration in Cooling'
        'Freeze Sentinel Request'               -> 'Freeze Sentinel'
    """
    # Strip the word wherever it appears as a complete word (word boundaries)
    result = re.sub(r'\b(?:Status|Request)\b', '', name, flags=re.IGNORECASE)
    # Collapse multiple spaces and strip surrounding whitespace
    result = re.sub(r'\s+', ' ', result).strip()
    return result


def _number_command_template(data_size: int, scaling_factor: int, signed: bool = False) -> str:
    """Return command_template for a number entity.

    When ``signed`` is True a modulo operation is applied so that negative
    values are converted to their two's-complement unsigned hex representation
    (e.g. -1 for an int16 becomes 'ffff').  Modulo is used instead of a
    bitwise-AND mask because Jinja2 does not support the ``&`` operator.
    """
    hex_chars = data_size * 2
    if signed:
        max_val = 1 << (data_size * 8)
        if scaling_factor > 1:
            return f"{{{{ '%0{hex_chars}x' % ((((value | float) * {scaling_factor}) | round | int) % {max_val}) }}}}"
        return f"{{{{ '%0{hex_chars}x' % ((value | int) % {max_val}) }}}}"
    if scaling_factor > 1:
        return f"{{{{ '%0{hex_chars}x' % (((value | float) * {scaling_factor}) | round | int) }}}}"
    return f"{{{{ '%0{hex_chars}x' % (value | int) }}}}"


# ---------------------------------------------------------------------------
# Shared helpers for HA-discovery data collection
# ---------------------------------------------------------------------------

def _get_primary_field(erd_by_id: Dict[str, Dict], paired_erd_str: str):
    """Return the first non-reserved, non-bitfield data field of the paired ERD.

    Returns None if the paired ERD is not found or has no suitable field.
    """
    if not paired_erd_str or paired_erd_str not in erd_by_id:
        return None
    erd_data = erd_by_id[paired_erd_str].get('data', [])
    for d in erd_data:
        if not _is_reserved_field(d.get('name', '')) and not _has_bits(d):
            return d
    return None

def _find_paired_field(request_field: Dict, paired_erd_str: str,
                       erd_by_id: Dict[str, Dict]) -> Optional[Dict]:
    """Find the matching field in the paired ERD by name or (offset, size).

    Returns the paired field dict, or None if no match is found.
    Strategy: (1) name match after stripping Request/Status suffix,
    (2) (offset, size) fallback, (3) None.
    """
    if not paired_erd_str or paired_erd_str not in erd_by_id:
        return None
    paired_data = erd_by_id[paired_erd_str].get('data', [])

    req_name = request_field.get('name', '')
    req_offset = request_field.get('offset', 0)
    req_size = request_field.get('size', 1)

    # Strip "Request"/"Status" suffix for name matching.
    def _strip_role(n: str) -> str:
        n = n.strip()
        for suffix in (' request', ' status'):
            if n.lower().endswith(suffix):
                return n[:len(n) - len(suffix)].strip()
        return n

    req_stripped = _strip_role(req_name)

    # (1) Name match.
    for d in paired_data:
        d_stripped = _strip_role(d.get('name', ''))
        if d_stripped == req_stripped:
            return d

    # (2) (offset, size) fallback.
    for d in paired_data:
        if d.get('offset', 0) == req_offset and d.get('size', 1) == req_size:
            return d

    return None


def _paired_field_vt(field: Dict, paired_erd_str: str, pair_role: str,
                     erd_by_id: Dict[str, Dict], scaling_factor: int) -> str:
    """Return a Jinja2 value_template for a field, using the paired status
    field's offset/size/type when pair_role is 'request'.

    For non-paired ERDs or status role, uses the field's own attributes.
    """
    if pair_role == 'request' and paired_erd_str and paired_erd_str in erd_by_id:
        paired = _find_paired_field(field, paired_erd_str, erd_by_id)
        if paired is not None:
            p_scaling = int(paired.get('scaling_factor') or scaling_factor)
            return _byte_subfield_value_template(paired, p_scaling)
    return _byte_subfield_value_template(field, scaling_factor)


def _paired_bitfield_vt(field: Dict, paired_erd_str: str, pair_role: str,
                        erd_by_id: Dict[str, Dict]) -> str:
    """Return a Jinja2 value_template for a bitfield sub-field, using the
    paired status field's offset/bits when pair_role is 'request'.

    For non-paired ERDs or status role, uses the field's own attributes.
    """
    if pair_role == 'request' and paired_erd_str and paired_erd_str in erd_by_id:
        paired = _find_paired_field(field, paired_erd_str, erd_by_id)
        if paired is not None:
            return _bitfield_sub_value_template(paired)
    return _bitfield_sub_value_template(field)

def _paired_switch_vt(field: Dict, paired_erd_str: str, pair_role: str,
                      erd_by_id: Dict[str, Dict]) -> str:
    """Return a raw hex extraction VT for a switch, using the paired status
    field's offset/size. Output matches state_on/state_off (e.g. '01'/'00').
    """
    if pair_role == 'request' and paired_erd_str and paired_erd_str in erd_by_id:
        paired = _find_paired_field(field, paired_erd_str, erd_by_id)
        if paired is not None:
            field = paired
    offset = field.get('offset', 0)
    size = field.get('size', 1)
    hex_start = offset * 2
    hex_end = (offset + size) * 2
    return f"{{{{ value[{hex_start}:{hex_end}] }}}}"


def _handle_single_erd(erd: Dict, erd_by_id: Dict[str, Dict],
                       collect, erd_id_int: int, display_name: str,
                       ha_domain: str, unit: str, device_class: str,
                       state_class: str, scaling_factor: int, data_size: int,
                       paired_erd_id: int, pair_role: str, paired_erd_str: str,
                       erd_data: List[Dict]) -> bool:
    """Handle classification == 'single'. Returns True if an entry was collected."""
    vt, ct, opts = '', '', ''
    min_val, max_val, step_val = 0.0, 0.0, 1.0

    if ha_domain == 'sensor':
        # Detect enum from either device_class or data field type.
        primary_type = _get_primary_data_type(erd_data)
        if device_class == 'enum' or primary_type == 'enum':
            ev, fs = _get_first_enum_field_info(erd_data)
            vt = _enum_sensor_value_template(ev, fs)
        elif primary_type == 'string':
            # String-type ERDs: MQTT payload is hex. Convert each byte
            # pair to plain ASCII, stripping trailing '_' padding.
            vt = _string_value_template(data_size)
        elif data_size <= 4:
            primary_type = _get_primary_data_type(erd_data)
            signed = _is_signed_type(primary_type)
            vt = _compute_sensor_value_template(
                scaling_factor, data_size, signed, _is_float32_type(primary_type)
            )
    elif ha_domain == 'switch':
        # For paired switches, read state from the status ERD's primary field.
        pf = _get_primary_field(erd_by_id, paired_erd_str)
        if pf is None:
            pf = {'name': '', 'type': 'u8', 'offset': 0, 'size': 1}
        vt = _paired_switch_vt(pf, paired_erd_str, pair_role, erd_by_id)
    elif ha_domain == 'select':
        ev, fs = _get_first_enum_field_info(erd_data)
        if ev:
            # For paired request ERDs, use the status field's enum values
            # for the VT so it decodes the actual appliance state.
            if pair_role == 'request' and paired_erd_str and paired_erd_str in erd_by_id:
                enum_field = next((d for d in erd_data if d.get('type') == 'enum'), None)
                if enum_field:
                    paired = _find_paired_field(enum_field, paired_erd_str, erd_by_id)
                    if paired and paired.get('values'):
                        ev = paired['values']
            opts, vt, ct = _select_options_and_templates(ev, fs)
        else:
            # No enum values to populate options; skip rather than emit
            # a broken select entity.
            return False
    elif ha_domain == 'number':
        pf = _get_primary_field(erd_by_id, paired_erd_str)
        if pf:
            p_scale = int(erd_by_id[paired_erd_str].get('scaling_factor') or 1)
            vt = _byte_subfield_value_template(pf, p_scale)
            signed = _is_signed_type(pf.get('type', 'u8'))
        elif paired_erd_str and paired_erd_str in erd_by_id:
            p_scale = int(erd_by_id[paired_erd_str].get('scaling_factor') or 1)
            paired_type = _get_primary_data_type(erd_by_id[paired_erd_str].get('data', []))
            signed = _is_signed_type(paired_type)
            vt = _compute_sensor_value_template(p_scale, data_size, signed)
        else:
            signed = _is_signed_type(_get_primary_data_type(erd_data))
            vt = _compute_sensor_value_template(scaling_factor, data_size, signed)
        ct = _number_command_template(data_size, scaling_factor, signed)
        # Compute min/max/step for number entities
        if pf:
            n_type = pf.get('type', 'u8')
            n_scale = int(erd_by_id[paired_erd_str].get('scaling_factor') or 1)
        elif paired_erd_str and paired_erd_str in erd_by_id:
            n_type = _get_primary_data_type(erd_by_id[paired_erd_str].get('data', []))
            n_scale = int(erd_by_id[paired_erd_str].get('scaling_factor') or 1)
        else:
            n_type = _get_primary_data_type(erd_data)
            n_scale = scaling_factor
        min_val, max_val, step_val = _compute_number_range(n_type, n_scale)
    # button: no templates

    # Allow overrides to supply a custom value_template that bypasses
    # the auto-generated one (e.g., combining multi-field u64 values).
    custom_vt = erd.get('value_template')
    if custom_vt:
        vt = custom_vt
    # For switch/binary_sensor, set payload_on/off and state_on/off to hex
    p_on = ''
    p_off = ''
    s_on = ''
    s_off = ''
    if ha_domain in ('switch', 'binary_sensor'):
        p_on = '01'
        p_off = '00'
        s_on = '01'
        s_off = '00'
    collect(erd_id_int, display_name, ha_domain, unit, device_class,
            state_class, scaling_factor, data_size, paired_erd_id,
            pair_role, vt, ct, opts, '',
            'box' if ha_domain == 'number' else '',
            p_on, p_off, s_on, s_off,
            min_val, max_val, step_val)
    return True


def _handle_byte_offset_erd(erd_by_id: Dict[str, Dict], collect,
                            erd_id_int: int, display_name: str,
                            ha_domain: str, unit: str, state_class: str,
                            scaling_factor: int, data_size: int,
                            pair_role: str, paired_erd_str: str,
                            erd_data: List[Dict]) -> None:
    """Handle classification == 'byte_offset'."""
    nr_fields = _get_non_reserved_fields(erd_data)
    for idx, field in enumerate(nr_fields):
        leaf = _clean_field_name(_leaf_field_name(field.get('name', '')))
        entity_name = f'{display_name} - {leaf}'
        fid = '' if idx == 0 else _field_slug(leaf)
        f_type = field.get('type', '')
        f_dc = field.get('device_class') or ''
        f_dev_cls = f_dc or ('enum' if f_type == 'enum' else '')
        f_unit = field.get('unit_of_measurement') or _infer_unit_from_field_name(leaf, unit)
        f_state_cls = field.get('state_class') or (state_class if f_type in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32') else '')
        # Don't inherit ERD-level unit/scaling for non-numeric fields
        # (e.g., enum status fields shouldn't inherit gal/min from a sibling).
        if f_type not in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32'):
            f_unit = ''
        # Use per-field pairing/domain if available (mixed-pairing ERDs)
        f_pair_role = field.get('pair_role') or pair_role
        f_paired_erd = field.get('paired_erd') or paired_erd_str
        f_paired_id = parse_erd_id(f_paired_erd) if f_paired_erd else 0
        f_ha_domain = field.get('ha_domain') or ha_domain
        # Per-field scaling — used for VT, CT, range, and stored in entry.
        f_scaling = int(field.get('scaling_factor') or scaling_factor)
        if f_type not in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32'):
            f_scaling = 1
        if f_ha_domain == 'binary_sensor' and f_type == 'enum':
            f_dev_cls = ''
            field_size = field.get('size', 1)
            vt = _compute_binary_sensor_value_template(field_size)
            opts, ct = '', ''
        elif f_ha_domain == 'select' and f_type == 'enum':
            f_dev_cls = ''
            field_size = field.get('size', 1)
            enum_vals = field.get('values', {})
            if enum_vals:
                opts, vt, ct = _select_options_and_templates(enum_vals, field_size)
            else:
                # No enum values; fall back to sensor-style VT
                vt = _enum_sensor_value_template(enum_vals, field_size)
                opts, ct = '', ''
        else:
            if f_ha_domain == 'switch':
                vt = _paired_switch_vt(field, f_paired_erd, f_pair_role, erd_by_id)
            else:
                vt = _paired_field_vt(field, f_paired_erd, f_pair_role, erd_by_id, f_scaling)
            opts, ct = '', ''
            if f_ha_domain == 'number' and f_pair_role == 'request':
                field_size = field.get('size', 1)
                signed = _is_signed_type(f_type)
                ct = _number_command_template(field_size, f_scaling, signed)
        # Compute min/max/step for number sub-fields
        f_min, f_max, f_step = 0.0, 0.0, 1.0
        if f_ha_domain == 'number':
            f_min, f_max, f_step = _compute_number_range(f_type, f_scaling)
        collect(erd_id_int, entity_name, f_ha_domain, f_unit, f_dev_cls,
                f_state_cls, f_scaling, data_size, f_paired_id,
                f_pair_role, vt, ct, opts, fid, '',
                '01' if f_ha_domain == 'switch' else '',
                '00' if f_ha_domain == 'switch' else '',
                '01' if f_ha_domain == 'switch' else '',
                '00' if f_ha_domain == 'switch' else '',
                f_min, f_max, f_step)


def _handle_bitfield_erd(collect, erd_id_int: int, display_name: str,
                         ha_domain: str, scaling_factor: int, data_size: int,
                         pair_role: str, paired_erd_str: str,
                         erd_data: List[Dict], erd_by_id: Dict[str, Dict]) -> None:
    """Handle classification == 'bitfield'."""
    for field in _get_non_reserved_fields(erd_data):
        leaf = _clean_field_name(_leaf_field_name(field.get('name', '')))
        fid = _field_slug(leaf)
        bits_size = field.get('bits', {}).get('size', 1)
        # Use per-field pairing/domain if available (mixed-pairing ERDs)
        f_pair_role = field.get('pair_role') or pair_role
        f_paired_erd = field.get('paired_erd') or paired_erd_str
        f_paired_id = parse_erd_id(f_paired_erd) if f_paired_erd else 0
        f_ha_domain = field.get('ha_domain') or ha_domain
        # For paired request ERDs, inherit the parent domain (switch/select/number)
        # so bitfield sub-entities are controllable, not read-only.
        if f_pair_role == 'request' and f_ha_domain in ('switch', 'select', 'number'):
            sub_domain = f_ha_domain
        else:
            sub_domain = 'binary_sensor' if bits_size == 1 else 'sensor'
        vt = _paired_bitfield_vt(field, f_paired_erd, f_pair_role, erd_by_id)
        b_p_on = '01' if sub_domain in ('binary_sensor', 'switch') else ''
        b_p_off = '00' if sub_domain in ('binary_sensor', 'switch') else ''
        b_s_on = '01' if sub_domain in ('binary_sensor', 'switch') else ''
        b_s_off = '00' if sub_domain in ('binary_sensor', 'switch') else ''
        f_scaling = int(field.get('scaling_factor') or scaling_factor)
        f_bf_dc = field.get('device_class') or ''
        collect(erd_id_int, f'{display_name} - {leaf}', sub_domain, '', f_bf_dc,
                '', f_scaling, data_size, f_paired_id, f_pair_role,
                vt, '', '', fid, '', b_p_on, b_p_off, b_s_on, b_s_off)


def _handle_mixed_erd(erd_by_id: Dict[str, Dict], collect,
                      erd_id_int: int, display_name: str,
                      ha_domain: str, unit: str, state_class: str,
                      scaling_factor: int, data_size: int,
                      pair_role: str, paired_erd_str: str,
                      erd_data: List[Dict]) -> None:
    """Handle classification == 'mixed'."""
    primary = next(
        (d for d in erd_data
         if not _has_bits(d) and not _is_reserved_field(d.get('name', ''))),
        None
    )
    if primary:
        p_type = primary.get('type', '')
        p_dc = primary.get('device_class') or ''
        p_dev_cls = p_dc or ('enum' if p_type == 'enum' else '')
        # Use per-field pairing/domain if available (mixed-pairing ERDs)
        p_pair_role = primary.get('pair_role') or pair_role
        p_paired_erd = primary.get('paired_erd') or paired_erd_str
        p_paired_id = parse_erd_id(p_paired_erd) if p_paired_erd else 0
        p_ha_domain = primary.get('ha_domain') or ha_domain
        # Per-field scaling — used for VT, CT, range, and stored in entry.
        p_scaling = int(primary.get('scaling_factor') or scaling_factor)

        # Skip the primary field if it's paired to a controllable request ERD
        # (switch/select/number) — the request ERD handles state+command.
        # Only generate the primary if it's unpaired or the paired request
        # ERD is not a controllable domain.
        skip_primary = False
        if p_pair_role == 'status' and p_paired_erd and p_paired_erd in erd_by_id:
            req = erd_by_id[p_paired_erd]
            if req.get('pair_role') == 'request' and req.get('ha_domain') in ('switch', 'select', 'number'):
                skip_primary = True

        if not skip_primary:
            if p_ha_domain == 'binary_sensor' and p_type == 'enum':
                # binary_sensor can't display enum labels; use ON/OFF
                p_dev_cls = ''
                p_field_size = primary.get('size', 1)
                p_vt = _compute_binary_sensor_value_template(p_field_size)
                p_ct = ''
            else:
                if p_ha_domain == 'switch':
                    p_vt = _paired_switch_vt(primary, p_paired_erd, p_pair_role, erd_by_id)
                    p_ct = ''
                else:
                    p_vt = _paired_field_vt(primary, p_paired_erd, p_pair_role, erd_by_id, p_scaling)
                    p_ct = ''
                    if p_ha_domain == 'number' and p_pair_role == 'request':
                        p_field_size = primary.get('size', 1)
                        p_signed = _is_signed_type(p_type)
                        p_ct = _number_command_template(p_field_size, p_scaling, p_signed)
            # Compute min/max/step for number primary fields
            p_min, p_max, p_step = 0.0, 0.0, 1.0
            if p_ha_domain == 'number':
                p_min, p_max, p_step = _compute_number_range(p_type, p_scaling)
            p_unit = primary.get('unit_of_measurement') or unit
            collect(erd_id_int, display_name, p_ha_domain, p_unit, p_dev_cls,
                    primary.get('state_class') or state_class, p_scaling, data_size, p_paired_id,
                    p_pair_role, p_vt, p_ct, '', '',
                    'box' if p_ha_domain == 'number' else '',
                    '01' if p_ha_domain == 'switch' else '',
                    '00' if p_ha_domain == 'switch' else '',
                    '01' if p_ha_domain == 'switch' else '',
                    '00' if p_ha_domain == 'switch' else '',
                    p_min, p_max, p_step)

    for field in [d for d in erd_data
                  if _has_bits(d) and not _is_reserved_field(d.get('name', ''))]:
        leaf = _clean_field_name(_leaf_field_name(field.get('name', '')))
        fid = _field_slug(leaf)
        bits_size = field.get('bits', {}).get('size', 1)
        # Use per-field pairing/domain if available (mixed-pairing ERDs)
        f_pair_role = field.get('pair_role') or pair_role
        f_paired_erd = field.get('paired_erd') or paired_erd_str
        f_paired_id = parse_erd_id(f_paired_erd) if f_paired_erd else 0
        f_ha_domain = field.get('ha_domain') or ha_domain
        # For paired request ERDs, inherit the parent domain (switch/select/number)
        # so bitfield sub-entities are controllable, not read-only.
        if f_pair_role == 'request' and f_ha_domain in ('switch', 'select', 'number'):
            sub_domain = f_ha_domain
        else:
            sub_domain = 'binary_sensor' if bits_size == 1 else 'sensor'
        vt = _paired_bitfield_vt(field, f_paired_erd, f_pair_role, erd_by_id)
        b_p_on = '01' if sub_domain in ('binary_sensor', 'switch') else ''
        b_p_off = '00' if sub_domain in ('binary_sensor', 'switch') else ''
        b_s_on = '01' if sub_domain in ('binary_sensor', 'switch') else ''
        b_s_off = '00' if sub_domain in ('binary_sensor', 'switch') else ''
        f_scaling = int(field.get('scaling_factor') or scaling_factor)
        f_bf_dc = field.get('device_class') or ''
        collect(erd_id_int, f'{display_name} - {leaf}', sub_domain, '', f_bf_dc,
                '', f_scaling, data_size, f_paired_id, f_pair_role,
                vt, '', '', fid, '', b_p_on, b_p_off, b_s_on, b_s_off)


def _handle_version_erd(collect, erd_id_int: int, display_name: str,
                        ha_domain: str, unit: str, device_class: str,
                        state_class: str, scaling_factor: int, data_size: int,
                        paired_erd_id: int, pair_role: str,
                        erd_data: List[Dict]) -> None:
    """Handle classification == 'version'."""
    nr_fields = _get_non_reserved_fields(erd_data)
    vt = _version_value_template(nr_fields)
    collect(erd_id_int, display_name, ha_domain, unit, device_class,
            state_class, scaling_factor, data_size, paired_erd_id,
            pair_role, vt, '', '', '', '', '', '', '', '')


def _handle_multi_board_version_erd(collect, erd_id_int: int, display_name: str,
                                     ha_domain: str, unit: str, device_class: str,
                                     state_class: str, scaling_factor: int,
                                     data_size: int, paired_erd_id: int,
                                     pair_role: str, erd_data: List[Dict]) -> None:
    """Handle classification == 'multi_board_version'."""
    groups = _group_multi_board_version_fields(erd_data)
    for group in groups:
        board = group['prefix']
        entity_name = f'{display_name} - {board} Version' if board else display_name
        vt = _version_value_template(group['version_fields'])
        fid = _field_slug(board) if board else ''
        collect(erd_id_int, entity_name, ha_domain, unit, device_class,
                state_class, scaling_factor, data_size, paired_erd_id,
                pair_role, vt, '', '', fid, '', '', '', '', '')

        # Add parametric version if both fields present
        param_fields = group['parametric_fields']
        if param_fields[0] is not None and param_fields[1] is not None:
            param_name = f'{display_name} - {board} Parametric Version' if board else f'{display_name} - Parametric Version'
            p_vt = _parametric_version_value_template(param_fields)
            if p_vt:
                param_fid = _field_slug(board + '_parametric') if board else 'parametric'
                collect(erd_id_int, param_name, ha_domain, unit, device_class,
                        state_class, scaling_factor, data_size, paired_erd_id,
                        pair_role, p_vt, '', '', param_fid, '', '', '', '', '')


def _collect_ha_discovery_entries(erds: List[Dict]) -> List[Dict]:
    """Process all ERDs with ha_domain metadata and return a list of entry dicts.

    Each dict has the keys: erd_id, name, domain, unit, device_class,
    state_class, scaling_factor, data_size, paired_erd_id, pair_role,
    value_template, command_template, options_json, field_id.

    This is the single source of truth for ha-discovery data; both the C header
    generator and the JSONL generator call this function.
    """
    erd_by_id: Dict[str, Dict] = {e['id']: e for e in erds}
    ha_erds = [e for e in erds if 'ha_domain' in e]
    entries: List[Dict] = []

    # Track (erd_id, field_id) to detect and resolve collisions.
    _field_id_counts: Dict[tuple, int] = {}

    def _make_unique_field_id(erd_id_int: int, field_id: str) -> str:
        """If field_id is empty or unique for this ERD, return as-is.
        Otherwise append a counter to disambiguate (e.g., 'auto_detergent_1')."""
        if not field_id:
            return field_id
        key = (erd_id_int, field_id)
        count = _field_id_counts.get(key, 0)
        _field_id_counts[key] = count + 1
        if count == 0:
            return field_id
        return f'{field_id}_{count}'

    def collect(erd_id_int: int, name: str, domain: str, unit: str,
                dev_cls: str, state_cls: str, scaling: int, d_size: int,
                paired_id: int, role: str, val_tmpl: str, cmd_tmpl: str,
                opts: str, field_id: str, mode: str = '',
                payload_on: str = '', payload_off: str = '',
                state_on: str = '', state_off: str = '',
                min_val: float = 0.0, max_val: float = 0.0, step_val: float = 1.0) -> None:
        # Skip availability/allowability metadata — not actionable in HA.
        combined = (name + ' ' + field_id).lower()
        # But don't skip real "allowed" values like "Allowed Setpoint".
        # Skip availability/allowability metadata, but not real data fields.
        # "Allowed Setpoint", "Minimum Allowed X", "Maximum Allowed X" are real data.
        if ('allowed' in combined and 'setpoint' not in combined and 'minimum allowed' not in combined and 'maximum allowed' not in combined) or 'available' in combined:
            return
        # Ensure field_id is unique within this ERD to avoid unique_id collisions.
        fid = _make_unique_field_id(erd_id_int, field_id)
        entries.append({
            'erd_id': erd_id_int,
            'name': name,
            'domain': domain,
            'unit': unit,
            'device_class': dev_cls,
            'state_class': state_cls,
            'scaling_factor': scaling,
            'data_size': d_size,
            'paired_erd_id': paired_id,
            'pair_role': role,
            'value_template': val_tmpl,
            'command_template': cmd_tmpl,
            'options_json': opts,
            'field_id': fid,
            'mode': mode,
            'payload_on': payload_on,
            'payload_off': payload_off,
            'state_on': state_on,
            'state_off': state_off,
            'min_val': min_val,
            'max_val': max_val,
            'step_val': step_val,
        })

    for erd in ha_erds:
        erd_id_int = parse_erd_id(erd['id'])
        name = erd.get('name', '')
        ha_domain = erd.get('ha_domain', '')
        unit = _unit_to_ha(erd.get('unit_of_measurement') or '')
        device_class = erd.get('device_class') or ''
        state_class = erd.get('state_class') or ''
        scaling_factor = int(erd.get('scaling_factor') or 1)
        pair_role = erd.get('pair_role') or ''
        paired_erd_str = erd.get('paired_erd') or ''
        paired_erd_id = parse_erd_id(paired_erd_str) if paired_erd_str else 0
        display_name = _strip_pair_role_word(name) if pair_role else name
        erd_data = erd.get('data', [])
        data_size = get_erd_byte_size(erd_data) or 1

        # Skip request ERDs without a valid status counterpart.
        if pair_role == 'request':
            if not (paired_erd_str and paired_erd_str in erd_by_id):
                continue

        # Skip status ERD if paired request is controllable and all fields paired.
        if pair_role == 'status' and paired_erd_str and paired_erd_str in erd_by_id:
            paired = erd_by_id[paired_erd_str]
            paired_role = paired.get('pair_role') or ''
            paired_domain = paired.get('ha_domain') or ''
            if paired_role == 'request' and paired_domain in ('switch', 'select', 'number'):
                all_paired = all(
                    f.get('paired_erd') == paired_erd_str
                    for f in erd_data if not _is_reserved_field(f.get('name', ''))
                )
                if all_paired:
                    continue

        # Determine classification
        if ha_domain in ('select', 'button'):
            nr_fields = _get_non_reserved_fields(erd_data)
            classification = 'single' if len(nr_fields) <= 1 else _classify_erd_data(erd_data)
        else:
            classification = _classify_erd_data(erd_data)
        # Allow overrides to force classification
        forced_classification = erd.get('force_classification')
        if forced_classification:
            classification = forced_classification

        # Dispatch to handler
        if classification == 'single':
            if not _handle_single_erd(erd, erd_by_id, collect, erd_id_int,
                                      display_name, ha_domain, unit, device_class,
                                      state_class, scaling_factor, data_size,
                                      paired_erd_id, pair_role, paired_erd_str,
                                      erd_data):
                continue
        elif classification == 'byte_offset':
            _handle_byte_offset_erd(erd_by_id, collect, erd_id_int, display_name,
                                    ha_domain, unit, state_class, scaling_factor,
                                    data_size, pair_role, paired_erd_str, erd_data)
        elif classification == 'bitfield':
            _handle_bitfield_erd(collect, erd_id_int, display_name, ha_domain,
                                 scaling_factor, data_size, pair_role,
                                 paired_erd_str, erd_data, erd_by_id)
        elif classification == 'mixed':
            _handle_mixed_erd(erd_by_id, collect, erd_id_int, display_name,
                              ha_domain, unit, state_class, scaling_factor,
                              data_size, pair_role, paired_erd_str, erd_data)
        elif classification == 'version':
            _handle_version_erd(collect, erd_id_int, display_name, ha_domain,
                                unit, device_class, state_class, scaling_factor,
                                data_size, paired_erd_id, pair_role, erd_data)
        elif classification == 'multi_board_version':
            _handle_multi_board_version_erd(collect, erd_id_int, display_name,
                                            ha_domain, unit, device_class,
                                            state_class, scaling_factor, data_size,
                                            paired_erd_id, pair_role, erd_data)
    return entries


# ---------------------------------------------------------------------------
# JSONL generation
# ---------------------------------------------------------------------------

def generate_ha_discovery_jsonl_by_category(erds: List[Dict]) -> Dict[str, str]:
    """Generate compact JSONL content grouped by appliance category.

    Returns a dict mapping category name -> JSONL string content.
    Each JSONL line is a compact JSON object with the pre-computed ha-discovery
    fields for one entity.  Fields that equal their default value are omitted to
    reduce file size.
    """
    entries = _collect_ha_discovery_entries(erds)

    categorized: Dict[str, list] = {cat: [] for cat in CATEGORIES}
    uncategorized = []
    for entry in entries:
        eid = entry['erd_id']
        matched = False
        for cat, (lo, hi) in CATEGORIES.items():
            if lo <= eid <= hi:
                categorized[cat].append(entry)
                matched = True
                break
        if not matched:
            uncategorized.append(entry)

    if uncategorized:
        ids = [f"0x{e['erd_id']:04x}" for e in uncategorized]
        print(f"WARNING: {len(uncategorized)} ERD(s) in undefined category gaps: "
              f"{', '.join(ids[:20])}{'...' if len(ids) > 20 else ''}",
              file=sys.stderr)

    result: Dict[str, str] = {}
    for cat in CATEGORIES:
        cat_entries = categorized[cat]
        if not cat_entries:
            continue
        lines = []
        for e in cat_entries:
            # --- entity validation ---
            domain = e['domain']

            # Skip select entities without options (broken in HA)
            if domain == 'select' and not e['options_json']:
                continue

            # Skip number entities with zero range (mn == mx, invalid in HA)
            if domain == 'number' and e.get('min_val') == e.get('max_val'):
                continue

            obj: Dict[str, Any] = {
                'i': f'{e["erd_id"]:04x}',
                'n': e['name'],
                'd': domain,
                'ds': e['data_size'],
            }
            # Omit fields that equal their defaults to save space
            if e['unit']:                         obj['u']  = e['unit']
            if e['device_class'] and _is_valid_device_class(domain, e['device_class']):
                obj['dc'] = e['device_class']
            if e['state_class']:                  obj['sc'] = e['state_class']
            if e['scaling_factor'] != 1:          obj['sf'] = e['scaling_factor']
            if domain == 'number' and e.get('min_val') is not None:      obj['mn'] = e['min_val']
            if domain == 'number' and e.get('max_val') is not None:      obj['mx'] = e['max_val']
            if domain == 'number' and e.get('step_val') is not None:     obj['st'] = e['step_val']
            if e['paired_erd_id']:                obj['p']  = f'{e["paired_erd_id"]:04x}'
            if e['pair_role']:                    obj['r']  = e['pair_role']
            if e['value_template']:               obj['vt'] = e['value_template']
            if e['command_template']:             obj['ct'] = e['command_template']
            if e['options_json']:                 obj['o']  = e['options_json']
            if e['field_id']:                     obj['fi'] = e['field_id']
            if e['mode']:                         obj['m']  = e['mode']
            if e['payload_on']:                   obj['pon'] = e['payload_on']
            if e['payload_off']:                  obj['poff'] = e['payload_off']
            if e['state_on']:                     obj['son'] = e['state_on']
            if e['state_off']:                    obj['soff'] = e['state_off']
            lines.append(json.dumps(obj, ensure_ascii=False, separators=(',', ':')))
        result[cat] = '\n'.join(lines) + '\n'

    return result


# ---------------------------------------------------------------------------
# File discovery and main
# ---------------------------------------------------------------------------

def find_erd_definitions_json() -> Optional[Path]:
    """Find the ERD definitions JSON using multiple search paths.

    Mirrors the search strategy in __init__.py::load_appliance_types().
    Returns the path if found, or None.
    """
    json_filename = "appliance_api_erd_definitions.json"
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    seen_paths = set()

    search_paths = [
        # Local submodule (for development with checked out repo)
        repo_root / 'lib' / 'public-appliance-api-documentation' / json_filename,
        # ESPHome library cache in user's home directory
        Path.home() / '.esphome' / 'external_files' / 'libraries' / 'public-appliance-api-documentation' / json_filename,
        # ESPHome library cache in /config (Home Assistant add-on)
        Path('/config/.esphome/external_files/libraries/public-appliance-api-documentation/' + json_filename),
        # ESPHome library cache relative to component (build directory)
        repo_root / '.esphome' / 'external_files' / 'libraries' / 'public-appliance-api-documentation' / json_filename,
        # Parent library path (external_components layout)
        repo_root / 'lib' / 'public-appliance-api-documentation' / json_filename,
    ]

    for p in search_paths:
        norm = str(p.resolve())
        if norm in seen_paths:
            continue
        seen_paths.add(norm)
        if p.exists():
            return p

    return None


def fetch_erd_definitions_from_github() -> Optional[dict]:
    """Fetch ERD definitions from GitHub as fallback."""
    import urllib.request as urllib
    url = "https://raw.githubusercontent.com/geappliances/public-appliance-api-documentation/main/appliance_api_erd_definitions.json"
    print(f"Fetching ERD definitions from GitHub: {url}", file=sys.stderr)
    try:
        with urllib.urlopen(url, timeout=10) as response:
            return json.loads(response.read().decode('utf-8'))
    except Exception as e:
        print(f"Failed to fetch from GitHub: {e}", file=sys.stderr)
        return None


def _build_erds_from_flat_list(flat_entries: List[Dict]) -> List[Dict]:
    """Convert the flat processed JSON list into grouped ERD objects.

    The processed JSON is a flat list where each entry represents one field
    of an ERD. Multiple entries may share the same erd_id. This function
    groups them by erd_id and builds the ERD structure expected by
    _collect_ha_discovery_entries.
    """
    from collections import OrderedDict

    # Group flat entries by erd_id, preserving order
    groups: Dict[str, List[Dict]] = OrderedDict()
    for entry in flat_entries:
        erd_id = entry.get('erd_id', '')
        if erd_id not in groups:
            groups[erd_id] = []
        groups[erd_id].append(entry)

    erds = []
    for erd_id, entries in groups.items():
        # Use the first entry to get ERD-level metadata
        first = entries[0]
        review = first.get('review') or {}

        # Build data array from field-level info
        data_fields = []
        for entry in entries:
            field = {
                'name': entry.get('field_name', ''),
                'type': entry.get('field_type', 'u8'),
                'offset': entry.get('field_offset', 0),
                'size': entry.get('field_size', 1),
            }
            if entry.get('field_values'):
                field['values'] = entry['field_values']
            if entry.get('field_bits'):
                field['bits'] = entry['field_bits']
            # Store per-field review metadata for mixed-pairing ERDs
            field_review = entry.get('review') or {}
            if field_review.get('paired_erd'):
                field['paired_erd'] = field_review['paired_erd']
            if field_review.get('pair_role'):
                field['pair_role'] = field_review['pair_role']
            if field_review.get('ha_domain'):
                field['ha_domain'] = field_review['ha_domain']
            if field_review.get('device_class') is not None:
                field['device_class'] = field_review['device_class'] or ''
            if field_review.get('state_class'):
                field['state_class'] = field_review['state_class']
            if field_review.get('scaling_factor') is not None:
                field['scaling_factor'] = field_review['scaling_factor']
            if field_review.get('unit_of_measurement'):
                field['unit_of_measurement'] = field_review['unit_of_measurement']
            if field_review.get('field_name'):
                field['name'] = field_review['field_name']
            data_fields.append(field)

        erd = {
            'id': erd_id,
            'name': first.get('erd_name', ''),
            'description': first.get('erd_description', ''),
            'operations': first.get('erd_operations', []),
            'data': data_fields,
        }

        # Add review metadata as top-level keys for _collect_ha_discovery_entries
        if review.get('ha_domain'):
            erd['ha_domain'] = review['ha_domain']
        if review.get('device_class') is not None:
            erd['device_class'] = review['device_class']
        if review.get('unit_of_measurement'):
            erd['unit_of_measurement'] = review['unit_of_measurement']
        if review.get('scaling_factor') is not None:
            erd['scaling_factor'] = review['scaling_factor']
        if review.get('state_class'):
            erd['state_class'] = review['state_class']
        if review.get('paired_erd'):
            erd['paired_erd'] = review['paired_erd']
        if review.get('pair_role'):
            erd['pair_role'] = review['pair_role']
        if review.get('force_classification'):
            erd['force_classification'] = review['force_classification']
        if review.get('value_template'):
            erd['value_template'] = review['value_template']

        erds.append(erd)

    return erds


# ---------------------------------------------------------------------------
# File discovery and main
# ---------------------------------------------------------------------------
def main():
    """Main entry point for the script."""
    import argparse
    parser = argparse.ArgumentParser(description="Generate HA discovery JSONL files.")
    parser.add_argument("--processed", type=str, default=None,
                        help="Path to processed ERD definitions JSON (flat list format).")
    parser.add_argument("--output-dir", type=str, default=None,
                        help="Output directory for JSONL files.")
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    output_dir = Path(args.output_dir) if args.output_dir else repo_root / 'ha_discovery'

    # Load ERD definitions
    data = None

    if args.processed:
        # Use processed JSON (flat list format from the pipeline)
        print(f"Reading processed ERD definitions from {args.processed}", file=sys.stderr)
        try:
            with open(args.processed, 'r') as f:
                data = json.load(f)
        except Exception as e:
            print(f"Failed to read {args.processed}: {e}", file=sys.stderr)
            sys.exit(1)
        # Processed JSON is a flat list; group by erd_id to build ERD objects
        erds = _build_erds_from_flat_list(data)
    else:
        json_file = find_erd_definitions_json()
        if json_file is not None:
            print(f"Reading ERD definitions from {json_file}", file=sys.stderr)
            try:
                with open(json_file, 'r') as f:
                    data = json.load(f)
            except Exception as e:
                print(f"Failed to read {json_file}: {e}", file=sys.stderr)
        else:
            print("Local ERD definitions not found, trying GitHub fallback...", file=sys.stderr)

        # Fallback to GitHub
        if data is None:
            data = fetch_erd_definitions_from_github()
            if data is None:
                print("Error: Could not find appliance_api_erd_definitions.json", file=sys.stderr)
                print("Tried local paths and GitHub. Check your network and git submodules.", file=sys.stderr)
                sys.exit(1)

        erds = data.get('erds', [])

    print(f"Found {len(erds)} ERD definitions", file=sys.stderr)

    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    # Generate JSONL by category
    jsonl_by_cat = generate_ha_discovery_jsonl_by_category(erds)
    total_entries = 0
    for cat, content in jsonl_by_cat.items():
        outfile = output_dir / f'{cat}.jsonl'
        with open(outfile, 'w', encoding='utf-8') as f:
            f.write(content)
        n = content.count('\n')
        total_entries += n
        print(f"  {cat}: {n} entities -> {cat}.jsonl ({len(content):,} bytes)", file=sys.stderr)

    print(f"\nTotal entities generated: {total_entries}", file=sys.stderr)
    print(f"Output directory: {output_dir}", file=sys.stderr)
    print("Done!", file=sys.stderr)


if __name__ == '__main__':
    main()
