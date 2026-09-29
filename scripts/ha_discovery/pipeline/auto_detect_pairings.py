#!/usr/bin/env python3
"""Auto-detect paired ERD (Request/Status) relationships at the field level.

Finds sequential ERD ID pairs where one has "Request" and the other "Status"
in the name, with matching base names. Then matches individual fields between
the request and status ERDs based on normalized name similarity.

Populates review.paired_erd and review.pair_role on matched field entries.

Usage:
    python3 scripts/auto_detect_pairings.py
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pipeline_utils import SCRIPT_DIR, REPO_ROOT, load_json



def strip_request_status(name):
    """Strip 'Request' or 'Status' suffix from an ERD name, handling trailing content.

    Also handles the compound form used by some ERDs, e.g.
    "Water Heater Boost Mode State - Requested/Desired" and
    "Water Heater Boost Mode State - Status/Actual".
    """
    n = re.sub(r'\s*[-–—:]?\s*(requested|request|status)\s*/\s*(desired|actual)\s*$',
               '', name, flags=re.IGNORECASE).strip()
    n = re.sub(r'\s*(request|status)\s*$', '', n, flags=re.IGNORECASE).strip()
    # Also strip trailing numbers after Request/Status (e.g. "Request 0" -> base)
    n = re.sub(r'\s*(request|status)\s*\d+\s*$', '', n, flags=re.IGNORECASE).strip()
    return n


def strip_requested_prefix(name):
    """Strip 'Requested ' prefix from an ERD name."""
    n = re.sub(r'^requested\s+', '', name, flags=re.IGNORECASE).strip()
    return n


def normalize_field_name(name):
    """Normalize a field name for comparison.

    Strips Request/Status suffixes, parenthetical hints,
    and dot-qualified prefixes (e.g. 'Settings.Cook Action' -> 'cook action').
    Preserves array indices [0], [1], etc. for matching.
    """
    n = name.lower()
    # Strip parenthetical hints at end FIRST (so "Status (0-100)" -> "Status")
    n = re.sub(r'\s*\(.*?\)\s*$', '', n)
    # Strip Request/Status suffixes
    n = re.sub(r'\s*(request|status)\s*$', '', n, flags=re.IGNORECASE)
    # Take the last dot-qualified segment (leaf name)
    parts = n.split('.')
    n = parts[-1].strip()
    # Normalize whitespace
    n = re.sub(r'\s+', ' ', n).strip()
    return n


def extract_array_index(name):
    """Extract array index from field name, or None if not an array field."""
    m = re.search(r'\[(\d+)\]', name)
    return int(m.group(1)) if m else None


def stem_word(w):
    """Simple stemming: remove common suffixes."""
    for suffix in ['ed', 'ing', 'ly', 'es', 's']:
        if w.endswith(suffix) and len(w) - len(suffix) >= 2:
            return w[:-len(suffix)]
    return w


def field_match_score(req_name, stat_name):
    """Score how well two field names match.

    Returns 0-100. >=50 is considered a match.
    """
    rn = normalize_field_name(req_name)
    sn = normalize_field_name(stat_name)

    # Exact match
    if rn == sn:
        return 100

    # One contains the other
    if rn in sn or sn in rn:
        return 70

    # Word overlap with stemming
    rw = set(stem_word(w) for w in rn.replace('/', ' ').split())
    sw = set(stem_word(w) for w in sn.replace('/', ' ').split())
    common = rw & sw

    if not common:
        return 0

    # Require at least one meaningful common word (not single char)
    meaningful = {w for w in common if len(w) > 1}
    if not meaningful:
        return 0

    min_len = min(len(rw), len(sw))
    ratio = len(meaningful) / min_len

    if ratio >= 0.8:
        return 60
    if ratio >= 0.5 and len(meaningful) >= 2:
        return 50

    return 0


def match_fields(req_fields, stat_fields):
    """Match request fields to status fields.

    Returns list of (req_entry, stat_entry, score) tuples for matched pairs.
    First tries exact normalized name match plus matching type, offset, and size.
    Falls back to fuzzy name matching via field_match_score when exact match fails.
    Uses greedy matching: each field can only be matched once.
    """
    matches = []
    used_stat = set()

    # Build index of status fields by normalized name
    stat_by_name = {}
    for sf in stat_fields:
        key = normalize_field_name(sf['field_name'])
        if key not in stat_by_name:
            stat_by_name[key] = []
        stat_by_name[key].append(sf)

    # Phase 1: exact structural match
    for rf in req_fields:
        key = normalize_field_name(rf['field_name'])
        if key not in stat_by_name:
            continue

        for sf in stat_by_name[key]:
            if id(sf) in used_stat:
                continue
            # Must have same type
            if rf['field_type'] != sf['field_type']:
                continue
            # Must have same byte offset
            if rf['field_offset'] != sf['field_offset']:
                continue
            # Must have same size
            if rf['field_size'] != sf['field_size']:
                continue
            # For bit-fields, must have same bit offset
            rb = rf.get('field_bits')
            sb = sf.get('field_bits')
            if rb is not None or sb is not None:
                if rb != sb:
                    continue
            # For array fields, must have same index
            ri = extract_array_index(rf['field_name'])
            si = extract_array_index(sf['field_name'])
            if ri is not None or si is not None:
                if ri != si:
                    continue

            matches.append((rf, sf, 100))
            used_stat.add(id(sf))
            break  # each request field matches at most one status field

    # Phase 2: fuzzy match for unmatched request fields
    for rf in req_fields:
        # Skip if already matched
        if any(m[0] is rf for m in matches):
            continue

        best_sf = None
        best_score = 0
        for sf in stat_fields:
            if id(sf) in used_stat:
                continue
            score = field_match_score(rf['field_name'], sf['field_name'])
            if score >= 50 and score > best_score:
                best_score = score
                best_sf = sf

        if best_sf is not None:
            matches.append((rf, best_sf, best_score))
            used_stat.add(id(best_sf))

    return matches

def find_erd_pairs(erd_by_id, erd_ids):
    """Find all Request/Status ERD pairs, including non-adjacent ones.

    Searches all pairs of ERDs (not just adjacent) to find Request/Status
    relationships. Uses name-based matching: strips 'Request'/'Status'
    suffixes and compares the base names.

    Also detects patterns where the status side has no keyword:
      - "Requested X" ↔ "X"
      - "X Request" ↔ "X"

    Returns list of (request_erd_id, status_erd_id) tuples.
    """
    pairs = []
    paired = set()
    n = len(erd_ids)

    # Build a base-name -> erd_id index for ERDs without request/status keywords
    bare_by_base: dict = {}
    for erd_id in erd_ids:
        erd = erd_by_id[erd_id]
        nl = erd['name'].lower()
        if 'request' not in nl and 'status' not in nl:
            bare_by_base[erd['name'].lower()] = erd_id

    for i in range(n):
        erd_id_i = erd_ids[i]
        if erd_id_i in paired:
            continue
        erd = erd_by_id[erd_id_i]
        nl = erd['name'].lower()
        has_req = 'request' in nl
        has_stat = 'status' in nl

        if not has_req and not has_stat:
            continue

        base1 = strip_request_status(erd['name'])

        for j in range(i + 1, n):
            erd_id_j = erd_ids[j]
            if erd_id_j in paired:
                continue
            next_erd = erd_by_id[erd_id_j]
            nxl = next_erd['name'].lower()
            next_req = 'request' in nxl
            next_stat = 'status' in nxl

            if not ((has_req and next_stat) or (has_stat and next_req)):
                continue

            base2 = strip_request_status(next_erd['name'])
            if base1.lower() != base2.lower():
                continue

            req_id = erd_id_i if has_req else erd_id_j
            stat_id = erd_id_j if has_req else erd_id_i

            pairs.append((req_id, stat_id))
            paired.add(req_id)
            paired.add(stat_id)
            break  # each ERD can only be in one pair

    # Second pass: pair unpaired request ERDs with bare counterparts.
    # Handles "Requested X" ↔ "X" and "X Request" ↔ "X" patterns.
    for erd_id in erd_ids:
        if erd_id in paired:
            continue
        erd = erd_by_id[erd_id]
        nl = erd['name'].lower()
        if 'request' not in nl:
            continue

        # Compute the base name by stripping request-related keywords
        base = strip_request_status(erd['name'])
        base = strip_requested_prefix(base)
        base_lower = base.lower()

        # Look for an unpaired bare ERD with the same base name
        if base_lower in bare_by_base:
            bare_id = bare_by_base[base_lower]
            if bare_id not in paired:
                pairs.append((erd_id, bare_id))
                paired.add(erd_id)
                paired.add(bare_id)

    return pairs


def apply_pairings(entries, erd_by_id, erd_ids):
    """Find ERD pairs and match fields, setting paired_erd/pair_role.

    Clears any existing pairings first, then re-computes from scratch.
    Returns (num_pairs, num_field_matches) counts.
    """
    # Clear existing pairings, using setdefault for entries without 'review'
    for e in entries:
        review = e.setdefault('review', {})
        review['paired_erd'] = None
        review['pair_role'] = None

    total_matches = 0
    pairs = find_erd_pairs(erd_by_id, erd_ids)

    for req_id, stat_id in pairs:
        req_entries = [e for e in entries if e['erd_id'] == req_id]
        stat_entries = [e for e in entries if e['erd_id'] == stat_id]

        matches = match_fields(req_entries, stat_entries)

        for req_entry, stat_entry, score in matches:
            req_review = req_entry.setdefault('review', {})
            stat_review = stat_entry.setdefault('review', {})
            req_review['paired_erd'] = stat_id
            req_review['pair_role'] = 'request'
            stat_review['paired_erd'] = req_id
            stat_review['pair_role'] = 'status'
            total_matches += 2

    return len(pairs), total_matches


def main():
    parser = argparse.ArgumentParser(
        description='Auto-detect paired ERD (Request/Status) relationships at field level.'
    )
    parser.add_argument(
        '--input',
        default=os.path.join(SCRIPT_DIR, '..', 'appliance_api_erd_definitions_processed.json'),
        help='Input processed file (default: appliance_api_erd_definitions_processed.json)',
    )
    parser.add_argument(
        '--output',
        default=os.path.join(SCRIPT_DIR, '..', 'appliance_api_erd_definitions_processed.json'),
        help='Output path (default: overwrites input)',
    )
    parser.add_argument(
        '--verbose',
        action='store_true',
        help='Print detailed matching info for each pair',
    )
    args = parser.parse_args()

    entries = load_json(args.input)

    # Build ERD index
    erd_by_id = {}
    for e in entries:
        erd_id = e['erd_id']
        if erd_id not in erd_by_id:
            erd_by_id[erd_id] = {
                'id': erd_id,
                'name': e['erd_name'],
                'fields': [],
                'operations': e['erd_operations'],
            }
        erd_by_id[erd_id]['fields'].append(e)

    erd_ids = sorted(erd_by_id.keys(), key=lambda x: int(x, 16))

    num_pairs, num_matches = apply_pairings(entries, erd_by_id, erd_ids)

    print(f"Found {num_pairs} Request/Status ERD pairs")
    print(f"Matched {num_matches} field entries ({num_matches // 2} field pairs)")

    # Show summary
    paired_entries = [e for e in entries if e['review'].get('paired_erd')]
    roles = {}
    for e in paired_entries:
        r = e['review']['pair_role']
        roles[r] = roles.get(r, 0) + 1
    print(f"Roles: {roles}")

    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(entries, f, indent=2, ensure_ascii=False)

    print(f"Output: {args.output}")


if __name__ == '__main__':
    main()