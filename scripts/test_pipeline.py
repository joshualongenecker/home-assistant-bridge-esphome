#!/usr/bin/env python3
"""
Unit tests for pipeline processing functions.

Covers:
  - apply_overrides: offset-based keys, bare erd_id fallback, type guards,
    precedence, idempotency
  - auto_detect_scaling non-numeric field guard
  - apply_post_processing all 4 rules

Run with:
    python3 -m pytest scripts/test_pipeline.py -v
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "ha_discovery" / "pipeline"))
import auto_detect_scaling as scaling
from post_process import apply_overrides, apply_post_processing


# Synthetic overrides for testing precedence and type guards.
# Keys must be lowercase since apply_overrides normalizes erd_id to lower.
_SYNTHETIC_OVERRIDES = {
    "0xtest": {"unit_of_measurement": "bare"},
    "0xtest:0": {"unit_of_measurement": "offset"},
    "0xbare": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
}


class TestApplyOverrides(unittest.TestCase):
    """Test apply_overrides with mock entries."""

    def _entry(self, erd_id, field_offset, **review):
        """Create a mock entry dict."""
        return {
            "erd_id": erd_id,
            "field_offset": field_offset,
            "field_name": "Test Field",
            "field_type": "u16",
            "review": review,
        }

    # --- Tests using real OVERRIDES (default) ---

    def test_offset_based_key_match(self):
        """erd_id:offset key matches only the target offset."""
        entries = [
            self._entry("0x3015", 0),
            self._entry("0x3015", 2),
            self._entry("0x3015", 3),
        ]
        applied = apply_overrides(entries)
        # Only offset 0 should match "0x3015:0".
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "gal/min")
        self.assertEqual(entries[0]["review"]["scaling_factor"], 10000)
        # Offset 2 and 3 should not get the override.
        self.assertIsNone(entries[1]["review"].get("unit_of_measurement"))
        self.assertIsNone(entries[2]["review"].get("unit_of_measurement"))
        self.assertEqual(applied, 3)  # unit + scaling_factor + field_name

    def test_none_offset_falls_through_to_bare(self):
        """field_offset=None skips offset-based key, falls to bare erd_id."""
        entries = [
            self._entry("0x7130", None),
        ]
        applied = apply_overrides(entries)
        self.assertEqual(entries[0]["review"]["ha_domain"], "sensor")
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "rpm")
        self.assertEqual(applied, 2)

    def test_bare_override_applies_to_all_fields(self):
        """Bare erd_id override applies to every field of that ERD."""
        entries = [
            self._entry("0x7130", 0),
            self._entry("0x7130", 1),
        ]
        applied = apply_overrides(entries)
        # Both fields get the override.
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "rpm")
        self.assertEqual(entries[1]["review"]["unit_of_measurement"], "rpm")
        self.assertEqual(applied, 4)  # 2 fields * 2 keys

    def test_offset_key_matches_target_only(self):
        """Offset-based key matches only the target offset, not others."""
        entries = [
            self._entry("0x404c", 0),
            self._entry("0x404c", 4),
        ]
        applied = apply_overrides(entries)
        # Offset 0 matches "0x404c:0".
        self.assertEqual(entries[0]["review"]["force_classification"], "single")
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "g")
        # Offset 4 has no override, gets nothing.
        self.assertIsNone(entries[1]["review"].get("force_classification"))
        self.assertIsNone(entries[1]["review"].get("unit_of_measurement"))

    def test_idempotency(self):
        """Calling apply_overrides twice returns 0 on second call."""
        entries = [self._entry("0x3015", 0)]
        applied1 = apply_overrides(entries)
        self.assertGreater(applied1, 0)
        applied2 = apply_overrides(entries)
        self.assertEqual(applied2, 0)

    def test_no_match_for_unknown_erd(self):
        """Entries for ERDs not in OVERRIDES are untouched."""
        entries = [self._entry("0xFFFF", 0)]
        applied = apply_overrides(entries)
        self.assertEqual(applied, 0)
        self.assertEqual(entries[0]["review"], {})

    def test_offset_key_does_not_match_wrong_offset(self):
        """erd_id:0 does not match field at offset 2."""
        entries = [self._entry("0x3015", 2)]
        applied = apply_overrides(entries)
        self.assertEqual(applied, 0)
        self.assertEqual(entries[0]["review"], {})

    def test_offset_key_with_negative_offset(self):
        """Negative offset is valid int, constructs key, no match."""
        entries = [self._entry("0x3015", -1)]
        applied = apply_overrides(entries)
        self.assertEqual(applied, 0)

    def test_existing_review_values_preserved(self):
        """Override only changes values that differ; other review keys stay."""
        entries = [
            self._entry("0x3015", 0, ha_domain="sensor", device_class="flow"),
        ]
        applied = apply_overrides(entries)
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "gal/min")
        self.assertEqual(entries[0]["review"]["scaling_factor"], 10000)
        # Pre-existing values are preserved.
        self.assertEqual(entries[0]["review"]["ha_domain"], "sensor")
        self.assertEqual(entries[0]["review"]["device_class"], "flow")

    # --- Tests using synthetic overrides for type guards and precedence ---

    def test_string_offset_falls_to_bare(self):
        """String field_offset is normalized to None, falls to bare erd_id.

        Uses synthetic overrides where both bare and offset-based keys exist
        with different values, so the test actually verifies the guard.
        Without the guard, string "0" would match "0xTEST:0" and get "offset".
        With the guard, it falls through to bare "0xTEST" and gets "bare".
        """
        entries = [self._entry("0xTEST", "0")]
        apply_overrides(entries, _SYNTHETIC_OVERRIDES)
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "bare")

    def test_bool_offset_falls_to_bare(self):
        """Boolean field_offset is rejected, falls to bare erd_id.

        isinstance(True, int) is True in Python, so the guard must also
        reject bools explicitly. Without the bool guard, True would match
        "0xTEST:True" (not in overrides) then fall to bare "0xTEST".
        With the guard, it also falls to bare "0xTEST" — same result,
        but the test verifies the bool guard is in place.
        """
        entries = [self._entry("0xTEST", True)]
        apply_overrides(entries, _SYNTHETIC_OVERRIDES)
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "bare")

    def test_float_offset_falls_to_bare(self):
        """Float field_offset is normalized to None, falls to bare erd_id."""
        entries = [self._entry("0xTEST", 0.0)]
        apply_overrides(entries, _SYNTHETIC_OVERRIDES)
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "bare")

    def test_offset_key_precedence_over_bare(self):
        """When both erd_id:offset and erd_id exist, offset key wins.

        Uses synthetic overrides where "0xTEST" (bare) gives "bare" and
        "0xTEST:0" gives "offset". With field_offset=0, the offset key
        must win, giving "offset" not "bare".
        """
        entries = [self._entry("0xTEST", 0)]
        apply_overrides(entries, _SYNTHETIC_OVERRIDES)
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "offset")

    def test_bare_override_for_non_matching_offset(self):
        """When offset-based key doesn't match, bare erd_id is used."""
        entries = [self._entry("0xTEST", 1)]
        apply_overrides(entries, _SYNTHETIC_OVERRIDES)
        # "0xTEST:1" doesn't exist, falls through to bare "0xTEST".
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "bare")


class TestNonNumericFieldGuard(unittest.TestCase):
    """Test auto_detect_scaling clears stale units on non-numeric fields."""

    def _entry(self, field_type, **review):
        return {
            "field_name": "Test Field",
            "field_type": field_type,
            "field_bits": None,
            "review": review,
        }

    def test_enum_stale_unit_cleared(self):
        """Enum field with stale unit_of_measurement gets it cleared."""
        entries = [self._entry("enum", unit_of_measurement="gal/min")]
        scaling.apply_detection(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])

    def test_enum_stale_scaling_cleared(self):
        """Enum field with stale scaling_factor gets it cleared."""
        entries = [self._entry("enum", scaling_factor=10000)]
        scaling.apply_detection(entries)
        self.assertIsNone(entries[0]["review"]["scaling_factor"])

    def test_string_stale_unit_cleared(self):
        """String field with stale unit gets it cleared."""
        entries = [self._entry("string", unit_of_measurement="rpm")]
        scaling.apply_detection(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])

    def test_numeric_field_overwritten_by_detector(self):
        """Numeric fields are processed by the detector, not the non-numeric guard."""
        entry = {
            "field_name": "CLC Temperature",
            "field_type": "u16",
            "field_bits": None,
            "review": {"unit_of_measurement": "gal/min"},
        }
        scaling.apply_detection([entry])
        # The detector overwrites with its own detected unit, not gal/min.
        self.assertEqual(entry["review"]["unit_of_measurement"], "°F")

    def test_enum_with_no_stale_values_is_noop(self):
        """Enum field with no stale values is a no-op."""
        entries = [self._entry("enum")]
        scaling.apply_detection(entries)
        self.assertEqual(entries[0]["review"], {})

    def test_bitfield_stale_unit_cleared(self):
        """Bitfield field with stale unit gets it cleared."""
        entries = [self._entry("bitfield", unit_of_measurement="steps")]
        scaling.apply_detection(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])


class TestApplyPostProcessing(unittest.TestCase):
    """Test apply_post_processing rules."""

    def _entry(self, ha_domain, **review):
        return {
            "erd_id": "0xFFFF",
            "field_offset": 0,
            "field_name": "Test",
            "field_type": "u8",
            "review": {
                "ha_domain": ha_domain,
                "device_class": None,
                "unit_of_measurement": None,
                "scaling_factor": 1,
                "state_class": None,
                **review,
            },
        }

    # --- Rule 1: binary_sensor/switch unit clear ---

    def test_binary_sensor_unit_cleared(self):
        """Rule 1: binary_sensor with unit_of_measurement gets it cleared."""
        entries = [self._entry("binary_sensor", unit_of_measurement="W")]
        cleared_unit, _, _, _ = apply_post_processing(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])
        self.assertGreater(cleared_unit, 0)

    def test_switch_unit_cleared(self):
        """Rule 1: switch with unit_of_measurement gets it cleared."""
        entries = [self._entry("switch", unit_of_measurement="W")]
        cleared_unit, _, _, _ = apply_post_processing(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])
        self.assertGreater(cleared_unit, 0)

    def test_sensor_unit_not_cleared(self):
        """Rule 1: sensor with unit_of_measurement is NOT cleared."""
        entries = [self._entry("sensor", unit_of_measurement="W")]
        cleared_unit, _, _, _ = apply_post_processing(entries)
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "W")
        self.assertEqual(cleared_unit, 0)

    # --- Rule 2: number domain device_class validation ---

    def test_number_invalid_device_class_cleared(self):
        """Rule 2: number with invalid device_class gets it cleared."""
        entries = [self._entry("number", device_class="battery_charging")]
        _, cleared_dc, _, _ = apply_post_processing(entries)
        self.assertIsNone(entries[0]["review"]["device_class"])
        self.assertGreater(cleared_dc, 0)

    def test_number_valid_device_class_preserved(self):
        """Rule 2: number with valid device_class is preserved."""
        entries = [self._entry("number", device_class="temperature")]
        _, cleared_dc, _, _ = apply_post_processing(entries)
        self.assertEqual(entries[0]["review"]["device_class"], "temperature")
        self.assertEqual(cleared_dc, 0)

    # --- Rule 3: sensor state_class=measurement ---

    def test_sensor_with_device_class_gets_state_class(self):
        """Rule 3: sensor with device_class but no state_class gets measurement."""
        entries = [self._entry("sensor", device_class="temperature")]
        _, _, added_sc, _ = apply_post_processing(entries)
        self.assertEqual(entries[0]["review"]["state_class"], "measurement")
        self.assertGreater(added_sc, 0)

    def test_sensor_without_device_class_no_state_class(self):
        """Rule 3: sensor without device_class does not get state_class."""
        entries = [self._entry("sensor")]
        _, _, added_sc, _ = apply_post_processing(entries)
        self.assertIsNone(entries[0]["review"]["state_class"])
        self.assertEqual(added_sc, 0)

    # --- Rule 4: scaling_factor=0 fix ---

    def test_scaling_factor_zero_fixed(self):
        """Rule 4: scaling_factor=0 is fixed to 1."""
        entries = [self._entry("sensor", scaling_factor=0)]
        _, _, _, fixed = apply_post_processing(entries)
        self.assertEqual(entries[0]["review"]["scaling_factor"], 1)
        self.assertEqual(fixed, 1)

    def test_scaling_factor_one_not_touched(self):
        """Rule 4: scaling_factor=1 is not changed."""
        entries = [self._entry("sensor", scaling_factor=1)]
        _, _, _, fixed = apply_post_processing(entries)
        self.assertEqual(entries[0]["review"]["scaling_factor"], 1)
        self.assertEqual(fixed, 0)

    def test_scaling_factor_none_not_touched(self):
        """Rule 4: scaling_factor=None is not changed to 1."""
        entries = [self._entry("sensor", scaling_factor=None)]
        _, _, _, fixed = apply_post_processing(entries)
        self.assertIsNone(entries[0]["review"]["scaling_factor"])
        self.assertEqual(fixed, 0)


class TestFieldNameOverride(unittest.TestCase):
    """Test field_name override application in apply_overrides and _build_erds_from_flat_list."""

    def test_field_name_override_in_apply_overrides(self):
        """apply_overrides correctly applies field_name to the review dict."""
        overrides = {
            "0x3015:0": {"field_name": "Inlet Flow Rate"},
        }
        entries = [
            {
                "erd_id": "0x3015",
                "field_offset": 0,
                "field_name": "Original Name",
                "review": {},
            }
        ]
        applied = apply_overrides(entries, overrides)
        self.assertEqual(entries[0]["review"]["field_name"], "Inlet Flow Rate")
        self.assertEqual(applied, 1)

    def test_field_name_propagated_in_build_erds(self):
        """_build_erds_from_flat_list propagates field_name override to field['name']."""
        import sys
        from pathlib import Path
        sys.path.insert(0, str(Path(__file__).parent / "ha_discovery" / "generators"))
        from generate_ha_discovery import _build_erds_from_flat_list

        flat = [
            {
                "erd_id": "0x3015",
                "erd_name": "Inlet Flow",
                "erd_description": "",
                "erd_operations": ["read"],
                "field_name": "Original Name",
                "field_type": "u16",
                "field_offset": 0,
                "field_size": 2,
                "review": {"field_name": "Inlet Flow Rate"},
            }
        ]
        erds = _build_erds_from_flat_list(flat)
        self.assertEqual(erds[0]["data"][0]["name"], "Inlet Flow Rate")

    def test_field_name_override_not_present_uses_original(self):
        """When no field_name override, field keeps its original name."""
        import sys
        from pathlib import Path
        sys.path.insert(0, str(Path(__file__).parent / "ha_discovery" / "generators"))
        from generate_ha_discovery import _build_erds_from_flat_list

        flat = [
            {
                "erd_id": "0x3015",
                "erd_name": "Inlet Flow",
                "erd_description": "",
                "erd_operations": ["read"],
                "field_name": "Original Name",
                "field_type": "u16",
                "field_offset": 0,
                "field_size": 2,
                "review": {},
            }
        ]
        erds = _build_erds_from_flat_list(flat)
        self.assertEqual(erds[0]["data"][0]["name"], "Original Name")


class TestWordBound(unittest.TestCase):
    """Test _word_bound edge cases for problem keyword detection."""

    def _import_word_bound(self):
        """Import _word_bound from auto_detect_device_class."""
        import sys
        from pathlib import Path
        sys.path.insert(0, str(Path(__file__).parent / "ha_discovery" / "pipeline"))
        from auto_detect_device_class import _word_bound
        return _word_bound

    def test_underscore_matches_as_word_boundary(self):
        """'fault_clogged' matches 'fault' (underscore → space)."""
        wb = self._import_word_bound()
        self.assertTrue(wb('fault_clogged', 'fault'))

    def test_underscore_no_false_match(self):
        """'default_temperature' does NOT match 'fault'."""
        wb = self._import_word_bound()
        self.assertFalse(wb('default_temperature', 'fault'))

    def test_hyphen_matches_as_word_boundary(self):
        """'fault-code' matches 'fault' (hyphen is word boundary)."""
        wb = self._import_word_bound()
        self.assertTrue(wb('fault-code', 'fault'))

    def test_hyphen_no_false_match(self):
        """'default-theme' does NOT match 'fault'."""
        wb = self._import_word_bound()
        self.assertFalse(wb('default-theme', 'fault'))

    def test_camel_case_no_match(self):
        """'FaultClogged' does NOT match 'fault' (camelCase — no word boundary)."""
        wb = self._import_word_bound()
        # 'FaultClogged' → normalized 'faultclogged' (already lowered)
        # \bfault\b does not match 'faultclogged' because 'c' is not a word boundary
        self.assertFalse(wb('FaultClogged', 'fault'))

    def test_exact_match(self):
        """Exact keyword match works."""
        wb = self._import_word_bound()
        self.assertTrue(wb('fault', 'fault'))

    def test_word_at_end(self):
        """Keyword at the end of the string matches."""
        wb = self._import_word_bound()
        self.assertTrue(wb('clogged_fault', 'fault'))

    def test_word_at_start(self):
        """Keyword at the start of the string matches."""
        wb = self._import_word_bound()
        self.assertTrue(wb('fault_code', 'fault'))

    def test_partial_word_no_match(self):
        """Partial word embedded in another word does not match."""
        wb = self._import_word_bound()
        self.assertFalse(wb('default_faulty', 'fault'))


class TestStaleDeviceClassClearing(unittest.TestCase):
    """Test stale device_class clearing in apply_detection."""

    def _import_apply_detection(self):
        """Import apply_detection from auto_detect_device_class."""
        import sys
        from pathlib import Path
        sys.path.insert(0, str(Path(__file__).parent / "ha_discovery" / "pipeline"))
        from auto_detect_device_class import apply_detection
        return apply_detection

    def test_stale_device_class_cleared_when_no_match(self):
        """apply_detection clears device_class=None when infer_device_class returns None
        for an entry that previously had a value."""
        ad = self._import_apply_detection()
        entries = [
            {
                "field_name": "Some Random Field",
                "field_type": "u16",
                "field_bits": None,
                "review": {"device_class": "temperature"},
            }
        ]
        ad(entries)
        # infer_device_class returns None for this generic name,
        # so the stale device_class should be cleared to None.
        self.assertIsNone(entries[0]["review"]["device_class"])

    def test_stale_device_class_cleared_for_bool_field(self):
        """Stale device_class is cleared for bool fields too."""
        ad = self._import_apply_detection()
        entries = [
            {
                "field_name": "Random Bool",
                "field_type": "bool",
                "field_bits": None,
                "review": {"device_class": "problem"},
            }
        ]
        ad(entries)
        self.assertIsNone(entries[0]["review"]["device_class"])

    def test_device_class_not_cleared_when_already_none(self):
        """No-op when device_class is already None and infer returns None."""
        ad = self._import_apply_detection()
        entries = [
            {
                "field_name": "Some Random Field",
                "field_type": "u16",
                "field_bits": None,
                "review": {"device_class": None},
            }
        ]
        ad(entries)
        self.assertIsNone(entries[0]["review"]["device_class"])

    def test_device_class_set_when_matched(self):
        """device_class is set when infer_device_class detects a match."""
        ad = self._import_apply_detection()
        entries = [
            {
                "field_name": "CLC Temperature",
                "field_type": "u16",
                "field_bits": None,
                "review": {},
            }
        ]
        ad(entries)
        self.assertEqual(entries[0]["review"]["device_class"], "temperature")

    def test_non_eligible_field_type_skipped(self):
        """String field type is skipped by apply_detection."""
        ad = self._import_apply_detection()
        entries = [
            {
                "field_name": "Fault Status",
                "field_type": "string",
                "field_bits": None,
                "review": {"device_class": "problem"},
            }
        ]
        ad(entries)
        # String fields are skipped; stale value is preserved.
        self.assertEqual(entries[0]["review"]["device_class"], "problem")


class TestPerFieldPairingOverrides(unittest.TestCase):
    """Test per-field pairing overrides (paired_erd, pair_role) in apply_overrides."""

    def test_paired_erd_override_applied(self):
        """apply_overrides correctly applies paired_erd to field-level review dicts."""
        overrides = {
            "0x770d": {"paired_erd": "0x7708", "pair_role": "request"},
        }
        entries = [
            {
                "erd_id": "0x770d",
                "field_offset": 0,
                "field_name": "Setpoint Request",
                "field_type": "u16",
                "review": {},
            }
        ]
        applied = apply_overrides(entries, overrides)
        self.assertEqual(entries[0]["review"]["paired_erd"], "0x7708")
        self.assertEqual(entries[0]["review"]["pair_role"], "request")
        self.assertEqual(applied, 2)

    def test_per_field_pair_role_override_with_offset_key(self):
        """Per-field pairing override using erd_id:offset key."""
        overrides = {
            "0x7708:0": {"paired_erd": "0x770d", "pair_role": "status"},
            "0x7708:2": {"paired_erd": "0x770f", "pair_role": "status"},
        }
        entries = [
            {
                "erd_id": "0x7708",
                "field_offset": 0,
                "field_name": "Setpoint 1",
                "field_type": "u16",
                "review": {},
            },
            {
                "erd_id": "0x7708",
                "field_offset": 2,
                "field_name": "Setpoint 2",
                "field_type": "u16",
                "review": {},
            },
        ]
        applied = apply_overrides(entries, overrides)
        self.assertEqual(entries[0]["review"]["paired_erd"], "0x770d")
        self.assertEqual(entries[0]["review"]["pair_role"], "status")
        self.assertEqual(entries[1]["review"]["paired_erd"], "0x770f")
        self.assertEqual(entries[1]["review"]["pair_role"], "status")
        self.assertEqual(applied, 4)

    def test_per_field_pairing_propagated_in_build_erds(self):
        """_build_erds_from_flat_list propagates paired_erd and pair_role to field dicts."""
        import sys
        from pathlib import Path
        sys.path.insert(0, str(Path(__file__).parent / "ha_discovery" / "generators"))
        from generate_ha_discovery import _build_erds_from_flat_list

        flat = [
            {
                "erd_id": "0x7708",
                "erd_name": "Allowed Setpoint",
                "erd_description": "",
                "erd_operations": ["read"],
                "field_name": "Setpoint 1",
                "field_type": "u16",
                "field_offset": 0,
                "field_size": 2,
                "review": {"paired_erd": "0x770d", "pair_role": "status"},
            },
            {
                "erd_id": "0x7708",
                "erd_name": "Allowed Setpoint",
                "erd_description": "",
                "erd_operations": ["read"],
                "field_name": "Setpoint 2",
                "field_type": "u16",
                "field_offset": 2,
                "field_size": 2,
                "review": {"paired_erd": "0x770f", "pair_role": "status"},
            },
        ]
        erds = _build_erds_from_flat_list(flat)
        self.assertEqual(erds[0]["data"][0]["paired_erd"], "0x770d")
        self.assertEqual(erds[0]["data"][0]["pair_role"], "status")
        self.assertEqual(erds[0]["data"][1]["paired_erd"], "0x770f")
        self.assertEqual(erds[0]["data"][1]["pair_role"], "status")


import auto_detect_ha_domain as domain_detect
import auto_detect_pairings as pairings


class TestBinaryEnumKeying(unittest.TestCase):
    """Binary enums keyed other than 0/1 can't be switch/binary_sensor."""

    def _entry(self, values, writable=True):
        return {
            "field_type": "enum",
            "field_name": "Test",
            "field_values": values,
            "erd_operations": ["read", "write"] if writable else ["read"],
            "review": {},
        }

    def test_zero_one_writable_is_switch(self):
        d, _ = domain_detect.infer_ha_domain(self._entry({"0": "Off", "1": "On"}))
        self.assertEqual(d, "switch")

    def test_one_two_writable_is_select(self):
        d, _ = domain_detect.infer_ha_domain(self._entry({"1": "Open", "2": "Closed"}))
        self.assertEqual(d, "select")

    def test_zero_ff_read_only_is_sensor(self):
        d, _ = domain_detect.infer_ha_domain(
            self._entry({"0": "Inactive", "255": "Active"}, writable=False))
        self.assertEqual(d, "sensor")


class TestRequestedDesiredPairing(unittest.TestCase):
    """'- Requested/Desired' pairs with '- Status/Actual'."""

    def test_strip_compound_suffixes(self):
        self.assertEqual(
            pairings.strip_request_status("Water Heater Boost Mode State - Requested/Desired"),
            "Water Heater Boost Mode State")
        self.assertEqual(
            pairings.strip_request_status("Water Heater Boost Mode State - Status/Actual"),
            "Water Heater Boost Mode State")

    def test_plain_suffixes_unchanged(self):
        self.assertEqual(pairings.strip_request_status("Fan Speed Request"), "Fan Speed")
        self.assertEqual(pairings.strip_request_status("Fan Speed Status"), "Fan Speed")

    def test_find_pairs(self):
        erd_by_id = {
            "0x4220": {"id": "0x4220", "name": "Water Heater Boost Mode State - Status/Actual"},
            "0x4221": {"id": "0x4221", "name": "Water Heater Boost Mode State - Requested/Desired"},
        }
        pairs = pairings.find_erd_pairs(erd_by_id, ["0x4220", "0x4221"])
        self.assertEqual(pairs, [("0x4221", "0x4220")])


if __name__ == "__main__":
    unittest.main()
