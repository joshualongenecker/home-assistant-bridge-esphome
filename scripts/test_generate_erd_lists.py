#!/usr/bin/env python3
"""
Tests for generate_erd_lists.py's appliance-type handling.

The generated applianceTypeToErdGroupTranslation[] table is indexed by
appliance type, and its category token decides which Home Assistant discovery
entities a bridge creates. These tests cover the generator's guardrails: an
appliance type the upstream enum gained but the shared mapping does not know
about must fail the build instead of being dropped.

Run with:
    python3 -m pytest scripts/test_generate_erd_lists.py -v
  or:
    python3 -m unittest scripts.test_generate_erd_lists -v
"""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import generate_erd_lists as gen
from appliance_type_categories import APPLIANCE_TYPE_CATEGORY, UNKNOWN_APPLIANCE_TYPE


def upstream_appliance_types():
    """The ERD 0x0008 enum as it ships in the documentation submodule."""
    path = (Path(__file__).resolve().parents[1] /
            "lib" / "public-appliance-api-documentation" /
            "appliance_api_erd_definitions.json")
    with open(path) as f:
        return gen.get_appliance_type_values(json.load(f)["erds"])


class TestApplianceGroupRows(unittest.TestCase):

    def test_every_upstream_appliance_type_has_a_category_decision(self):
        known = {int(k) for k in upstream_appliance_types() if int(k) != UNKNOWN_APPLIANCE_TYPE}
        missing = sorted(known - set(APPLIANCE_TYPE_CATEGORY))
        self.assertEqual([], missing,
                         f"appliance types with no entry in APPLIANCE_TYPE_CATEGORY: {missing}")

    def test_unknown_appliance_type_is_not_emitted(self):
        rows = gen.build_appliance_group_rows(upstream_appliance_types())
        self.assertNotIn(UNKNOWN_APPLIANCE_TYPE, [row[0] for row in rows])

    def test_rows_are_dense_and_indexed_by_type(self):
        rows = gen.build_appliance_group_rows(upstream_appliance_types())
        self.assertEqual(list(range(len(rows))), [row[0] for row in rows])

    def test_new_enum_value_without_a_mapping_fails_loudly(self):
        # Pretend the upstream enum gained a type that nobody mapped: the
        # generator must refuse rather than emit a table that skips it.
        values = dict(upstream_appliance_types())
        values["59"] = "New Thing"
        with self.assertRaises(ValueError) as ctx:
            gen.build_appliance_group_rows(values)
        self.assertIn("APPLIANCE_TYPE_CATEGORY", str(ctx.exception))
        self.assertIn("New Thing", str(ctx.exception))

    def test_enum_gap_fails_loudly(self):
        # Remove a type from the middle of the enum; the mapping entry for it
        # has to go too, or the stale-key check fires first.
        original = dict(APPLIANCE_TYPE_CATEGORY)
        APPLIANCE_TYPE_CATEGORY.pop(1)
        try:
            values = dict(upstream_appliance_types())
            del values["1"]
            with self.assertRaises(ValueError) as ctx:
                gen.build_appliance_group_rows(values)
            self.assertIn("gap", str(ctx.exception))
        finally:
            APPLIANCE_TYPE_CATEGORY.clear()
            APPLIANCE_TYPE_CATEGORY.update(original)

    def test_stale_mapping_entry_fails_loudly(self):
        # The mirror case: upstream removed a type the mapping still covers.
        original = dict(APPLIANCE_TYPE_CATEGORY)
        APPLIANCE_TYPE_CATEGORY[99] = "range"
        try:
            with self.assertRaises(ValueError) as ctx:
                gen.build_appliance_group_rows(upstream_appliance_types())
            self.assertIn("no longer defines", str(ctx.exception))
            self.assertIn("99", str(ctx.exception))
        finally:
            APPLIANCE_TYPE_CATEGORY.clear()
            APPLIANCE_TYPE_CATEGORY.update(original)

    def test_unknown_category_token_fails_loudly(self):
        original = dict(APPLIANCE_TYPE_CATEGORY)
        APPLIANCE_TYPE_CATEGORY[0] = "waterheaterz"
        try:
            with self.assertRaises(ValueError) as ctx:
                gen.build_appliance_group_rows(upstream_appliance_types())
            self.assertIn("waterheaterz", str(ctx.exception))
        finally:
            APPLIANCE_TYPE_CATEGORY.clear()
            APPLIANCE_TYPE_CATEGORY.update(original)

    def test_category_token_maps_to_the_matching_erd_array(self):
        for _type_value, _name, token in gen.build_appliance_group_rows(upstream_appliance_types()):
            if token is None:
                continue
            self.assertIn(token, gen.CATEGORY_CPP)


class TestApplianceTypeMap(unittest.TestCase):

    def test_generated_map_returns_string_literals(self):
        header = gen.generate_appliance_type_map(
            [{"id": "0x0008", "data": [{"values": {"0": "Water Heater", "255": "Unknown"}}]}])
        self.assertIn("inline const char* appliance_type_to_string(uint8_t appliance_type) {", header)
        self.assertNotIn("#include <string>", header)
        self.assertIn('case 0: return "WaterHeater";', header)
        self.assertIn('default: return "Unknown";', header)


if __name__ == "__main__":
    unittest.main()
