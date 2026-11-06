#!/usr/bin/env python3
"""
Script to generate C header files from the public-appliance-api-documentation library.

Generates the following files:
  1. erd_lists.h (from appliance_api_erd_definitions.json):
     ERD lists organized by appliance type for the polling bridge discovery
     phase, plus the POLLING_LIST_MAX_SIZE constant.

  2. appliance_api_feature_lists.h (from appliance_api.json):
     Feature API descriptors that map feature bits in ERDs 0x0092-0x0097 and 0x0109-0x010D to
     the associated ERD lists. Used at runtime to build the valid ERD set when
     appliance_api_parsing is enabled.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Set

from appliance_type_categories import APPLIANCE_TYPE_CATEGORY, UNKNOWN_APPLIANCE_TYPE



def parse_erd_id(erd_id_str: str) -> int:
    """Convert ERD ID string (e.g., '0x0001') to integer."""
    return int(erd_id_str, 16)


def categorize_erds(erds: List[Dict]) -> Dict[str, List[int]]:
    """
    Categorize ERDs by hex range into appliance types.
    
    Ranges:
    0x0000 to 0x0FFF: common ERDs (all appliance types)
    0x1000 to 0x1FFF: refrigeration ERDs
    0x2000 to 0x2FFF: laundry ERDs
    0x3000 to 0x3FFF: dishwasher ERDs
    0x4000 to 0x4FFF: waterHeater ERDs
    0x5000 to 0x5FFF: range ERDs (i.e., stoves, cooktops, ovens, etc)
    0x7000 to 0x7FFF: air conditioning ERDs (i.e., mini split, Zoneline, etc)
    0x8000 to 0x8FFF: water filter ERDs
    0x9000 to 0x9FFF: small appliance ERDs (i.e., coffee makers, etc)
    0xD000 to 0xDFFF: energy ERDs (all appliance types)
    """
    categories = {
        'common': set(),
        'refrigeration': set(),
        'laundry': set(),
        'dishWasher': set(),
        'waterHeater': set(),
        'range': set(),
        'airConditioning': set(),
        'waterFilter': set(),
        'smallAppliance': set(),
        'energy': set()
    }
    
    for erd in erds:
        erd_id = parse_erd_id(erd['id'])
        
        if 0x0000 <= erd_id <= 0x0FFF:
            categories['common'].add(erd_id)
        elif 0x1000 <= erd_id <= 0x1FFF:
            categories['refrigeration'].add(erd_id)
        elif 0x2000 <= erd_id <= 0x2FFF:
            categories['laundry'].add(erd_id)
        elif 0x3000 <= erd_id <= 0x3FFF:
            categories['dishWasher'].add(erd_id)
        elif 0x4000 <= erd_id <= 0x4FFF:
            categories['waterHeater'].add(erd_id)
        elif 0x5000 <= erd_id <= 0x5FFF:
            categories['range'].add(erd_id)
        elif 0x7000 <= erd_id <= 0x7FFF:
            categories['airConditioning'].add(erd_id)
        elif 0x8000 <= erd_id <= 0x8FFF:
            categories['waterFilter'].add(erd_id)
        elif 0x9000 <= erd_id <= 0x9FFF:
            categories['smallAppliance'].add(erd_id)
        elif 0xD000 <= erd_id <= 0xDFFF:
            categories['energy'].add(erd_id)
    
    # Convert sets to sorted lists
    return {key: sorted(list(value)) for key, value in categories.items()}


def format_erd_list(erds: List[int], indent: int = 2) -> str:
    """Format a list of ERDs as C array elements."""
    if not erds:
        return ""
    
    lines = []
    indent_str = ' ' * indent
    
    for erd in erds:
        lines.append(f"{indent_str}0x{erd:04x},")
    
    return '\n'.join(lines)


# (category_key, array_name, count_name, description). category_key.lower() is
# the discovery category token shared with the HA pipeline
# (ha_discovery/<token>.jsonl, pipeline/ha_constants.CATEGORIES) and with
# scripts/appliance_type_categories.py.
CATEGORY_INFO = [
    ('common', 'commonErds', 'commonErdCount', '0x0000 to 0x0FFF: common ERDs (all appliance types)'),
    ('refrigeration', 'refrigerationErds', 'refrigerationErdCount', '0x1000 to 0x1FFF: refrigeration ERDs'),
    ('laundry', 'laundryErds', 'laundryErdCount', '0x2000 to 0x2FFF: laundry ERDs'),
    ('dishWasher', 'dishWasherErds', 'dishWasherErdCount', '0x3000 to 0x3FFF: dishwasher ERDs'),
    ('waterHeater', 'waterHeaterErds', 'waterHeaterErdCount', '0x4000 to 0x4FFF: waterHeater ERDs'),
    ('range', 'rangeErds', 'rangeErdCount', '0x5000 to 0x5FFF: range ERDs (stoves, cooktops, ovens, etc)'),
    ('airConditioning', 'airConditioningErds', 'airConditioningErdCount', '0x7000 to 0x7FFF: air conditioning ERDs (mini split, Zoneline, etc)'),
    ('waterFilter', 'waterFilterErds', 'waterFilterErdCount', '0x8000 to 0x8FFF: water filter ERDs'),
    ('smallAppliance', 'smallApplianceErds', 'smallApplianceErdCount', '0x9000 to 0x9FFF: small appliance ERDs (coffee makers, etc)'),
    ('energy', 'energyErds', 'energyErdCount', '0xD000 to 0xDFFF: energy ERDs (all appliance types)')
]

# Discovery category token -> (array_name, count_name) C symbols.
CATEGORY_CPP = {key.lower(): (array_name, count_name)
                for key, array_name, count_name, _ in CATEGORY_INFO}


def get_appliance_type_values(erds: List[Dict]) -> Dict[str, str]:
    """Return the ERD 0x0008 (Appliance Type) enum: value string -> display name."""
    erd = next((e for e in erds if e.get('id') == '0x0008'), None)
    if erd is None:
        raise ValueError("ERD 0x0008 (Appliance Type) not found in ERD definitions")
    return erd['data'][0]['values']


def build_appliance_group_rows(appliance_type_values: Dict[str, str]):
    """Rows for applianceTypeToErdGroupTranslation[], one per appliance type.

    The table is indexed by appliance type, so every value from 0 up to the
    largest enum value needs a category. A type the upstream enum gained but
    the mapping does not know about is an error rather than a skip: silently
    dropping it is exactly how appliances stopped getting their category ERDs.
    """
    display = {int(k): v for k, v in appliance_type_values.items()
               if int(k) != UNKNOWN_APPLIANCE_TYPE}
    if not display:
        raise ValueError("ERD 0x0008 (Appliance Type) enum has no values")
    bad_tokens = sorted({t for t in APPLIANCE_TYPE_CATEGORY.values()
                         if t is not None and t not in CATEGORY_CPP})
    if bad_tokens:
        raise ValueError(f"APPLIANCE_TYPE_CATEGORY uses unknown category tokens: {bad_tokens}")
    rows = []
    for type_value in range(max(display) + 1):
        if type_value not in display:
            raise ValueError(
                f"appliance type enum has a gap at {type_value}; the generated "
                "table is indexed by appliance type and cannot skip values")
        if type_value not in APPLIANCE_TYPE_CATEGORY:
            raise ValueError(
                f"appliance type {type_value} ({display[type_value]!r}) is missing from "
                "APPLIANCE_TYPE_CATEGORY in scripts/appliance_type_categories.py; "
                "add it (None if it has no category-specific ERDs) and regenerate")
        rows.append((type_value, display[type_value], APPLIANCE_TYPE_CATEGORY[type_value]))
    stale = sorted(set(APPLIANCE_TYPE_CATEGORY) - set(display))
    if stale:
        print(f"WARNING: APPLIANCE_TYPE_CATEGORY covers types the enum no longer defines: {stale}")
    return rows


def generate_header(categories: Dict[str, List[int]], polling_list_max_size: int) -> str:
    """Generate the erd_lists.h header file with extern const declarations."""
    header = f"""/*!
 * @file
 * @brief Erd lists for various appliances
 * 
 * This file is auto-generated from appliance_api_erd_definitions.json
 * Do not edit this file manually. Run scripts/generate_erd_lists.py to regenerate.
 */

#ifndef ERD_LISTS_H
#define ERD_LISTS_H

#include "tiny_erd.h"

// Maximum number of ERDs that can be held in the polling list.
// Sized for the worst case: common ERDs + energy ERDs + appliance API feature ERDs + largest appliance-specific ERD list.
#define POLLING_LIST_MAX_SIZE {polling_list_max_size}

"""
    
    # Generate each category — extern const declarations
    for category_key, array_name, count_name, description in CATEGORY_INFO:
        header += f"// {description}\n"
        header += f"extern const tiny_erd_t {array_name}[];\n"
        header += f"extern const uint16_t {count_name};\n\n"

    # Appliance API feature bit ERDs
    header += "// Appliance API feature bit ERDs (all appliance types)\n"
    header += "// These ERDs carry feature bitmaps used to determine which appliance API features are active.\n"
    header += "// Not generated from appliance_api_erd_definitions.json; hard-coded here.\n"
    header += "extern const tiny_erd_t applianceApiFeatureErds[];\n"
    header += "extern const uint16_t applianceApiFeatureErdCount;\n\n"

    # Add the lookup table structure
    header += """typedef struct {
  // Category-specific ERD list; NULL when the type has no category.
  const tiny_erd_t* erdList;
  uint16_t erdCount;
  // Discovery category token (matches ha_discovery_categories[].name); NULL when none.
  const char* category;
} applianceTypeToErdListAndCount_t;

extern const applianceTypeToErdListAndCount_t applianceTypeToErdGroupTranslation[];
extern const uint16_t maximumApplianceType;
#endif
"""
    
    return header


def generate_erd_lists_cpp(categories: Dict[str, List[int]],
                           appliance_type_values: Dict[str, str]) -> str:
    """Generate the erd_lists.cpp companion file with actual array definitions."""
    cpp = """#include "erd_lists.h"

"""

    for category_key, array_name, count_name, description in CATEGORY_INFO:
        erds = categories[category_key]
        cpp += f"// {description}\n"
        cpp += f"const tiny_erd_t {array_name}[] = {{\n"
        if erds:
            cpp += format_erd_list(erds)
            cpp += "\n"
        cpp += "};\n"
        cpp += f"const uint16_t {count_name} = sizeof({array_name}) / sizeof({array_name}[0]);\n\n"

    # Appliance API feature bit ERDs
    appliance_api_feature_erds = [
        (0x0092, 'Common feature API'),
        (0x0093, 'Appliance feature API group 0'),
        (0x0094, 'Appliance feature API group 1'),
        (0x0095, 'Appliance feature API group 2'),
        (0x0096, 'Appliance feature API group 3'),
        (0x0097, 'Appliance feature API group 4'),
        (0x0109, 'Appliance feature API group 5'),
        (0x010a, 'Appliance feature API group 6'),
        (0x010b, 'Appliance feature API group 7'),
        (0x010c, 'Appliance feature API group 8'),
        (0x010d, 'Appliance feature API group 9'),
    ]
    cpp += "// Appliance API feature bit ERDs (all appliance types)\n"
    cpp += "// These ERDs carry feature bitmaps used to determine which appliance API features are active.\n"
    cpp += "// Not generated from appliance_api_erd_definitions.json; hard-coded here.\n"
    cpp += "const tiny_erd_t applianceApiFeatureErds[] = {\n"
    for erd_id, comment in appliance_api_feature_erds:
        cpp += f"  0x{erd_id:04x},  // {comment}\n"
    cpp += "};\n"
    cpp += "const uint16_t applianceApiFeatureErdCount = sizeof(applianceApiFeatureErds) / sizeof(applianceApiFeatureErds[0]);\n\n"

    # Lookup table, indexed by appliance type. Derived from the upstream
    # ERD 0x0008 enum plus APPLIANCE_TYPE_CATEGORY; see
    # scripts/appliance_type_categories.py for how each category was chosen.
    cpp += "const applianceTypeToErdListAndCount_t applianceTypeToErdGroupTranslation[] = {\n"
    for type_value, display_name, token in build_appliance_group_rows(appliance_type_values):
        if token is None:
            cpp += (f"  {{ nullptr, 0, nullptr }}, "
                    f"// 0x{type_value:02X} = {display_name} (no category-specific ERDs)\n")
        else:
            array_name, count_name = CATEGORY_CPP[token]
            cpp += (f"  {{ {array_name}, {count_name}, \"{token}\" }}, "
                    f"// 0x{type_value:02X} = {display_name}\n")
    cpp += "};\n"
    cpp += ("const uint16_t maximumApplianceType = sizeof(applianceTypeToErdGroupTranslation) / "
            "sizeof(applianceTypeToErdGroupTranslation[0]);\n")


    return cpp




def sanitize_name_for_cpp(name: str) -> str:
    """Sanitize a feature API name for use as a C++ identifier (all lowercase)."""
    result = re.sub(r'[^a-zA-Z0-9]', '_', name)
    result = re.sub(r'_+', '_', result)
    return result.strip('_').lower()


def make_array_names(valid_feature_apis):
    """Return a dict mapping (api_key, ver, feature_index) -> unique array name."""
    result = {}
    for key, api in valid_feature_apis:
        api_sanitized = sanitize_name_for_cpp(api['name'])
        for ver, ver_data in api.get('versions', {}).items():
            candidates = {}
            for i, feature in enumerate(ver_data.get('features', [])):
                feat_sanitized = sanitize_name_for_cpp(feature['name'])
                base = f"appliance_api_{api_sanitized}_v{ver}_{feat_sanitized}_erds"
                candidates.setdefault(base, []).append((i, int(feature['mask'], 16)))
            for base, entries in candidates.items():
                if len(entries) == 1:
                    idx, _ = entries[0]
                    result[(key, ver, idx)] = base
                else:
                    for idx, mask in entries:
                        result[(key, ver, idx)] = f"{base[:-len('_erds')]}_m{mask:08x}_erds"
    return result


def collect_erds_for_feature(feature: Dict) -> List[int]:
    """Collect all ERD IDs for a single feature (required + optional)."""
    erds: Set[int] = set()
    for erd_entry in feature.get('required', []) + feature.get('optional', []):
        erds.add(parse_erd_id(erd_entry['erd']))
    return sorted(erds)


def collect_all_erds_for_common_feature(feature: Dict) -> List[int]:
    """Collect all ERD IDs for a single common feature."""
    erds: Set[int] = set()
    for erd_entry in feature.get('required', []) + feature.get('optional', []):
        erds.add(parse_erd_id(erd_entry['erd']))
    return sorted(erds)


def generate_appliance_api_feature_lists_header(appliance_api_data: Dict) -> str:
    """Generate the appliance_api_feature_lists.h header file with extern const declarations."""

    lines: List[str] = []
    lines.append("/*!")
    lines.append(" * @file")
    lines.append(" * @brief Appliance API feature descriptors mapping feature bits to ERD lists")
    lines.append(" *")
    lines.append(" * This file is auto-generated from appliance_api.json")
    lines.append(" * Do not edit this file manually. Run scripts/generate_erd_lists.py to regenerate.")
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef APPLIANCE_API_FEATURE_LISTS_H")
    lines.append("#define APPLIANCE_API_FEATURE_LISTS_H")
    lines.append("")
    lines.append("#include \"tiny_erd.h\"")
    lines.append("#include <stdint.h>")
    lines.append("")

    # -------------------------------------------------------------------------
    # Common features (ERD 0x0092)
    # -------------------------------------------------------------------------
    lines.append("// ============================================================================")
    lines.append("// Common features (ERD 0x0092)")
    lines.append("// Each feature is identified by a bitmask within the 4-byte ERD value")
    lines.append("// ============================================================================")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("  uint32_t bit_mask;")
    lines.append("  const char* name;")
    lines.append("  const tiny_erd_t* erds;")
    lines.append("  uint16_t erd_count;")
    lines.append("} common_feature_descriptor_t;")
    lines.append("")

    common = appliance_api_data.get('common', {})
    all_common_features: List[Dict] = []
    for ver_data in common.get('versions', {}).values():
        all_common_features.extend(ver_data.get('features', []))

    # Generate extern declarations for each common feature
    for feature in all_common_features:
        sanitized = sanitize_name_for_cpp(feature['name'])
        erds = collect_all_erds_for_common_feature(feature)
        array_name = f"common_feature_{sanitized}_erds"
        if erds:
            lines.append(f"extern const tiny_erd_t {array_name}[];")
        else:
            lines.append(f"extern const tiny_erd_t* {array_name};")
    lines.append("")

    # Master common feature descriptor array declaration
    lines.append("extern const common_feature_descriptor_t common_feature_descriptors[];")
    lines.append("extern const uint16_t common_feature_descriptor_count;")
    lines.append("")

    # -------------------------------------------------------------------------
    # Appliance Feature APIs (ERDs 0x0093-0x0097 and 0x0109-0x010D)
    # -------------------------------------------------------------------------
    lines.append("// ============================================================================")
    lines.append("// Appliance Feature APIs (ERDs 0x0093-0x0097 and 0x0109-0x010D)")
    lines.append("// Each ERD value is: [2B featureType][2B version][4B feature bitmap]")
    lines.append("// One descriptor per feature per version; matched at runtime by featureType+version.")
    lines.append("// ============================================================================")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("  uint16_t feature_type;  // appliance API type (bytes 0-1 of ERD value)")
    lines.append("  uint16_t version;       // API version (bytes 2-3 of ERD value)")
    lines.append("  uint32_t bit_mask;      // feature bitmap mask (within bytes 4-7 of ERD value)")
    lines.append("  const char* name;       // human-readable \"ApiName / FeatureName\" label")
    lines.append("  const tiny_erd_t* erds;")
    lines.append("  uint16_t erd_count;")
    lines.append("} appliance_feature_api_descriptor_t;")
    lines.append("")

    feature_apis = appliance_api_data.get('featureApis', {})

    valid_feature_apis = [
        (key, api)
        for key, api in feature_apis.items()
        if (api['featureType'] >> 8) <= 9
    ]


    array_names = make_array_names(valid_feature_apis)

    # Generate extern declarations for each feature array
    for key, api in valid_feature_apis:
        for ver, ver_data in api.get('versions', {}).items():
            for i, feature in enumerate(ver_data.get('features', [])):
                array_name = array_names[(key, ver, i)]
                erds = collect_erds_for_feature(feature)
                if erds:
                    lines.append(f"extern const tiny_erd_t {array_name}[];")
                else:
                    lines.append(f"extern const tiny_erd_t* {array_name};")
    lines.append("")

    # Master descriptor array declaration
    lines.append("extern const appliance_feature_api_descriptor_t appliance_feature_api_descriptors[];")
    lines.append("extern const uint16_t appliance_feature_api_descriptor_count;")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    return "\n".join(lines)


def generate_appliance_api_feature_lists_cpp(appliance_api_data: Dict) -> str:
    """Generate the appliance_api_feature_lists.cpp companion file with actual definitions."""

    lines: List[str] = []
    lines.append("#include \"appliance_api_feature_lists.h\"")
    lines.append("")

    # -------------------------------------------------------------------------
    # Common features (ERD 0x0092)
    # -------------------------------------------------------------------------
    lines.append("// ============================================================================")
    lines.append("// Common features (ERD 0x0092)")
    lines.append("// ============================================================================")
    lines.append("")

    common = appliance_api_data.get('common', {})
    all_common_features: List[Dict] = []
    for ver_data in common.get('versions', {}).values():
        all_common_features.extend(ver_data.get('features', []))

    # Generate ERD arrays for each common feature
    for feature in all_common_features:
        sanitized = sanitize_name_for_cpp(feature['name'])
        erds = collect_all_erds_for_common_feature(feature)
        array_name = f"common_feature_{sanitized}_erds"
        if erds:
            lines.append(f"const tiny_erd_t {array_name}[] = {{")
            for erd_id in erds:
                lines.append(f"  0x{erd_id:04x},")
            lines.append("};")
        else:
            lines.append(f"const tiny_erd_t* {array_name} __attribute__((unused)) = nullptr;")
        lines.append("")

    # Generate master common feature descriptor array
    lines.append("const common_feature_descriptor_t common_feature_descriptors[] = {")
    for feature in all_common_features:
        sanitized = sanitize_name_for_cpp(feature['name'])
        array_name = f"common_feature_{sanitized}_erds"
        mask_val = int(feature['mask'], 16)
        erds = collect_all_erds_for_common_feature(feature)
        count = len(erds)
        null_ptr = "nullptr" if count == 0 else array_name
        lines.append(f"  {{0x{mask_val:08x}, \"{feature['name']}\", {null_ptr}, {count}}},")
    lines.append("};")
    lines.append("const uint16_t common_feature_descriptor_count =")
    lines.append("  sizeof(common_feature_descriptors) / sizeof(common_feature_descriptors[0]);")
    lines.append("")

    # -------------------------------------------------------------------------
    # Appliance Feature APIs (ERDs 0x0093-0x0097 and 0x0109-0x010D)
    # -------------------------------------------------------------------------
    lines.append("// ============================================================================")
    lines.append("// Appliance Feature APIs (ERDs 0x0093-0x0097 and 0x0109-0x010D)")
    lines.append("// ============================================================================")
    lines.append("")

    feature_apis = appliance_api_data.get('featureApis', {})

    valid_feature_apis = [
        (key, api)
        for key, api in feature_apis.items()
        if (api['featureType'] >> 8) <= 9
    ]


    array_names = make_array_names(valid_feature_apis)

    # Generate one ERD array per feature per version per appliance API.
    for key, api in valid_feature_apis:
        for ver, ver_data in api.get('versions', {}).items():
            for i, feature in enumerate(ver_data.get('features', [])):
                array_name = array_names[(key, ver, i)]
                erds = collect_erds_for_feature(feature)
                if erds:
                    lines.append(f"const tiny_erd_t {array_name}[] = {{")
                    for erd_id in erds:
                        lines.append(f"  0x{erd_id:04x},")
                    lines.append("};")
                    lines.append("")
                else:
                    lines.append(f"const tiny_erd_t* {array_name} __attribute__((unused)) = nullptr;")
                    lines.append("")

    # Generate master descriptor array
    lines.append("const appliance_feature_api_descriptor_t appliance_feature_api_descriptors[] = {")
    for key, api in valid_feature_apis:
        ft = api['featureType']
        for ver, ver_data in api.get('versions', {}).items():
            for i, feature in enumerate(ver_data.get('features', [])):
                array_name = array_names[(key, ver, i)]
                mask_val = int(feature['mask'], 16)
                erds = collect_erds_for_feature(feature)
                count = len(erds)
                null_ptr = "nullptr" if count == 0 else array_name
                label = f"{api['name']} / {feature['name']}"
                lines.append(
                    f"  {{0x{ft:04x}, {ver}, 0x{mask_val:08x}, \"{label}\", {null_ptr}, {count}}},"
                )
    lines.append("};")
    lines.append("const uint16_t appliance_feature_api_descriptor_count =")
    lines.append("  sizeof(appliance_feature_api_descriptors) / sizeof(appliance_feature_api_descriptors[0]);")
    lines.append("")

    return "\n".join(lines)


def generate_appliance_type_map(erds: List[Dict]) -> str:
    """Generate the appliance_type_map.h header from ERD 0x0008 (Appliance Type).

    Maps each appliance-type value to a PascalCase identifier. The identifier is
    part of the generated device ID (applianceType_model_serial), so it must be
    stable across regenerations. Naming rule: plain PascalCase — split the display
    name on non-alphanumeric characters and capitalize the first letter of each
    word. The value is returned as a string literal, not a std::string: the only
    caller concatenates it into the device ID and nothing owns the pointer.
    """
    values = get_appliance_type_values(erds)

    def to_identifier(name: str) -> str:
        words = re.split(r'[^a-zA-Z0-9]+', name)
        return ''.join(w[0].upper() + w[1:] for w in words if w)

    lines = [
        "/*!",
        " * @file",
        " * @brief Appliance type (ERD 0x0008) to display-name identifier mapping",
        " *",
        " * This file is auto-generated from appliance_api_erd_definitions.json (ERD 0x0008).",
        " * Do not edit this file manually. Run scripts/generate_erd_lists.py to regenerate.",
        " */",
        "",
        "#ifndef APPLIANCE_TYPE_MAP_H",
        "#define APPLIANCE_TYPE_MAP_H",
        "",
        "#include <cstdint>",
        "",
        "inline const char* appliance_type_to_string(uint8_t appliance_type) {",
        "  switch (appliance_type) {",
    ]
    for key in sorted(values, key=int):
        lines.append(f'    case {key}: return "{to_identifier(values[key])}";')
    lines.append('    default: return "Unknown";')
    lines.append("  }")
    lines.append("}")
    lines.append("")
    lines.append("#endif // APPLIANCE_TYPE_MAP_H")
    return "\n".join(lines) + "\n"


def find_json_file(filename: str, component_dir: str) -> Optional[str]:
    """Search for a JSON file from the public-appliance-api-documentation library.

    Tries multiple locations in order of preference, matching the strategy
    used by load_appliance_types() in __init__.py.
    """
    search_paths = []
    seen = set()

    # 1. Local submodule (development checkout)
    p = os.path.normpath(os.path.join(component_dir, "..", "..", "lib",
                                      "public-appliance-api-documentation", filename))
    search_paths.append(p)
    seen.add(p)

    # 2. ESPHome library cache in user home
    home_dir = os.path.expanduser("~")
    p = os.path.join(home_dir, ".esphome", "external_files", "libraries",
                     "public-appliance-api-documentation", filename)
    if p not in seen:
        search_paths.append(p)
        seen.add(p)

    # 3. ESPHome library cache in /config (Home Assistant add-on)
    p = os.path.join("/config", ".esphome", "external_files", "libraries",
                     "public-appliance-api-documentation", filename)
    if p not in seen:
        search_paths.append(p)
        seen.add(p)

    # 4. ESPHome library cache relative to component (build directory)
    p = os.path.normpath(os.path.join(component_dir, "..", "..", ".esphome",
                                      "external_files", "libraries",
                                      "public-appliance-api-documentation", filename))
    if p not in seen:
        search_paths.append(p)
        seen.add(p)

    # 5. Parent lib directory (alternative location)
    parent_dir = os.path.dirname(os.path.dirname(component_dir))
    p = os.path.normpath(os.path.join(parent_dir, "lib",
                                      "public-appliance-api-documentation", filename))
    if p not in seen:
        search_paths.append(p)
        seen.add(p)

    for path in search_paths:
        if os.path.exists(path):
            return path

    return None


def main():
    """Main entry point for the script."""
    parser = argparse.ArgumentParser(
        description="Generate C header files from appliance API documentation."
    )
    parser.add_argument(
        "--erd-definitions",
        help="Path to appliance_api_erd_definitions.json",
    )
    parser.add_argument(
        "--appliance-api",
        help="Path to appliance_api.json",
    )
    parser.add_argument(
        "--component-dir",
        default=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                             "..", "components", "geappliances_bridge")),
        help="Path to the geappliances_bridge component directory",
    )
    parser.add_argument(
        "--output-dir",
        help="Override output directory for generated headers (default: component-dir)",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    component_dir = os.path.normpath(args.component_dir)
    output_dir = os.path.normpath(args.output_dir) if args.output_dir else component_dir

    # Resolve JSON file paths: CLI arg > auto-search > local submodule
    def resolve_json(filename, cli_arg):
        if cli_arg and os.path.exists(cli_arg):
            return Path(cli_arg)
        found = find_json_file(filename, component_dir)
        if found:
            return Path(found)
        return repo_root / "lib" / "public-appliance-api-documentation" / filename

    # -------------------------------------------------------------------------
    # Generate erd_lists.h from appliance_api_erd_definitions.json
    # -------------------------------------------------------------------------
    json_file = resolve_json("appliance_api_erd_definitions.json", args.erd_definitions)
    output_file = Path(output_dir) / "erd_lists.h"

    if not json_file.exists():
        print(f"Error: Could not find {json_file}", file=sys.stderr)
        print("Make sure git submodules are initialized (git submodule update --init).", file=sys.stderr)
        sys.exit(1)

    # Read and parse JSON
    print(f"Reading ERD definitions from {json_file}")
    with open(json_file, 'r') as f:
        data = json.load(f)

    erds = data.get('erds', [])
    print(f"Found {len(erds)} ERD definitions")

    # Categorize ERDs
    categories = categorize_erds(erds)

    # Print statistics
    print("\nERD counts by category:")
    for category, erd_list in categories.items():
        print(f"  {category}: {len(erd_list)}")

    # Calculate required POLLING_LIST_MAX_SIZE.
    # The polling list holds: common ERDs + energy ERDs + appliance API feature ERDs + appliance-specific ERDs.
    # Use the worst case across all appliance-specific categories.
    appliance_specific_categories = [
        'waterHeater', 'laundry', 'refrigeration', 'smallAppliance',
        'range', 'dishWasher', 'airConditioning', 'waterFilter'
    ]
    max_appliance_erds = max(len(categories[cat]) for cat in appliance_specific_categories)
    common_erds_count = len(categories['common'])
    energy_erds_count = len(categories['energy'])
    appliance_api_feature_erd_count = 11  # 0x0092-0x0097 and 0x0109-0x010D
    polling_list_max_size = common_erds_count + energy_erds_count + appliance_api_feature_erd_count + max_appliance_erds
    print(f"\nPolling list size check:")
    print(f"  common ERDs: {common_erds_count}, energy ERDs: {energy_erds_count}, appliance API feature ERDs: {appliance_api_feature_erd_count}, max appliance ERDs: {max_appliance_erds}")
    print(f"  POLLING_LIST_MAX_SIZE: {polling_list_max_size}")

    # Generate header (includes POLLING_LIST_MAX_SIZE)
    header_content = generate_header(categories, polling_list_max_size)
    cpp_content = generate_erd_lists_cpp(categories, get_appliance_type_values(erds))

    # Write output
    print(f"\nWriting generated header to {output_file}")
    with open(output_file, 'w') as f:
        f.write(header_content)

    cpp_output_file = Path(output_dir) / "erd_lists.cpp"
    print(f"Writing generated cpp to {cpp_output_file}")
    with open(cpp_output_file, 'w') as f:
        f.write(cpp_content)

    # -------------------------------------------------------------------------
    # Generate appliance_type_map.h from ERD 0x0008 (Appliance Type)
    # -------------------------------------------------------------------------
    type_map_content = generate_appliance_type_map(erds)
    type_map_output_file = Path(output_dir) / "appliance_type_map.h"
    print(f"Writing generated appliance type map to {type_map_output_file}")
    with open(type_map_output_file, 'w') as f:
        f.write(type_map_content)

    # -------------------------------------------------------------------------
    # Generate appliance_api_feature_lists.h from appliance_api.json
    # -------------------------------------------------------------------------
    api_json_file = resolve_json("appliance_api.json", args.appliance_api)
    api_output_file = Path(output_dir) / "appliance_api_feature_lists.h"

    if not api_json_file.exists():
        print(f"Error: Could not find {api_json_file}", file=sys.stderr)
        print("Make sure git submodules are initialized or network is available for GitHub fallback.", file=sys.stderr)
        sys.exit(1)

    print(f"\nReading appliance API definitions from {api_json_file}")
    with open(api_json_file, 'r') as f:
        appliance_api_data = json.load(f)

    feature_apis = appliance_api_data.get('featureApis', {})
    common_features = appliance_api_data.get('common', {})
    valid_feature_apis = [api for api in feature_apis.values() if (api['featureType'] >> 8) <= 9]
    print(f"Found {len(feature_apis)} feature APIs ({len(valid_feature_apis)} within ERDs 0x0093-0x0097 and 0x0109-0x010D)")
    common_ver_count = sum(
        len(vd.get('features', []))
        for vd in common_features.get('versions', {}).values()
    )
    print(f"Found {common_ver_count} common features (ERD 0x0092)")
    api_header_content = generate_appliance_api_feature_lists_header(appliance_api_data)
    api_cpp_content = generate_appliance_api_feature_lists_cpp(appliance_api_data)

    print(f"\nWriting generated header to {api_output_file}")
    with open(api_output_file, 'w') as f:
        f.write(api_header_content)

    api_cpp_output_file = Path(output_dir) / "appliance_api_feature_lists.cpp"
    print(f"Writing generated cpp to {api_cpp_output_file}")
    with open(api_cpp_output_file, 'w') as f:
        f.write(api_cpp_content)

    print("Done!")


if __name__ == '__main__':
    main()
